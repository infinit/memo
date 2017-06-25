#include <infinit/filesystem/File.hh>

#include <utility>

#ifdef INFINIT_WINDOWS
#include <fcntl.h>
#endif

#include <sys/stat.h> // S_IMFT...

#include <boost/algorithm/string/predicate.hpp>

#include <elle/cast.hh>
#include <elle/os/environ.hh>

#include <elle/cryptography/random.hh>
#include <elle/cryptography/SecretKey.hh>

#ifdef INFINIT_WINDOWS
#undef stat
#endif

#include <elle/serialization/json.hh>

#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/umbrella.hh>
#include <infinit/filesystem/xattribute.hh>

#include <elle/bench.hh>
#include <elle/cast.hh>
#include <elle/os/environ.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>

ELLE_LOG_COMPONENT("infinit.filesystem.File");

namespace infinit
{
  namespace filesystem
  {
    namespace
    {
      std::string print_mode(int m)
      {
        auto res = std::string{};
        res += (m & 0400) ? 'r' : '-';
        res += (m & 0200) ? 'w' : '-';
        res += (m & 0100) ? 'x' : '-';
        res += (m & 0040) ? 'r' : '-';
        res += (m & 0020) ? 'w' : '-';
        res += (m & 0010) ? 'x' : '-';
        res += (m & 0004) ? 'r' : '-';
        res += (m & 0002) ? 'w' : '-';
        res += (m & 0001) ? 'x' : '-';
        return res;
      }
    }

    FileConflictResolver::FileConflictResolver(elle::serialization::SerializerIn& s,
                                               elle::Version const& v)
    {
      this->serialize(s, v);
    }

    FileConflictResolver::FileConflictResolver()
      : _model(nullptr)
    {}

    FileConflictResolver::FileConflictResolver(bfs::path path, model::Model* model,
                         WriteTarget target)
      : _path(path)
      , _model(model)
      , _target(target)
    {}

    std::unique_ptr<Block>
    FileConflictResolver::operator()(Block& b,
                                     Block& current)
    {
      ELLE_LOG_SCOPE(
        "conflict: the file \"%s\" was modified since last read. Your"
        " changes will overwrite previous modifications", this->_path);
      auto cd = FileData(_path, current, {true, true}, 0);
      auto od = FileData(_path, b, {true, true}, 0);
      od.merge(cd, _target);
      // write od data into current block
      elle::Buffer serdata;
      {
        elle::IOStream os(serdata.ostreambuf());
        elle::serialization::binary::SerializerOut output(os);
        output.serialize("header", od._header);
        output.serialize("fat", od._fat);
        output.serialize("data", od._data);
      }
      auto block = elle::cast<MutableBlock>::runtime(current.clone());
      if (_target & WriteTarget::perms)
      {
        // acl permission changes are handled by a different resolver
        auto perms = dynamic_cast<ACLBlock&>(b).get_world_permissions();
        dynamic_cast<ACLBlock&>(*block)
          .set_world_permissions(perms.first, perms.second);
      }
      block->data(serdata);
      return elle::cast<Block>::runtime(block);
    }

    void
    FileConflictResolver::serialize(elle::serialization::Serializer& s,
                                    elle::Version const& version)
    {
      ELLE_DEBUG("FileConflictResolver: serialize in %s", version);
      std::string spath = this->_path.string();
      s.serialize("path", spath);
      this->_path = spath;
      if (version >= elle::Version(0, 5, 5))
        s.serialize("target", _target, elle::serialization::as<int>());
      else if (s.in())
        _target = WriteTarget::all | WriteTarget::block;
      if (s.in())
      {
        infinit::model::doughnut::Doughnut* model = nullptr;
        const_cast<elle::serialization::Context&>(s.context()).get(model);
        ELLE_ASSERT(model);
        this->_model = model;
      }
    }

    std::string
    FileConflictResolver::description() const
    {
      return elle::sprintf("edit file %s", this->_path);
    }

    namespace
    {
      template <typename T>
      using SerializationRegister =
        elle::serialization::Hierarchy<model::ConflictResolver>::
        Register<T>;

      const SerializationRegister<FileConflictResolver> _register_fcr("fcr");
    }

    void
    File::chmod(mode_t mode)
    {
      ELLE_DEBUG("chmod to %s", print_mode(mode));
      Node::chmod(mode);
      ELLE_DEBUG("current mode: %s", print_mode(_filedata->_header.mode));
    }

    void
    File::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }

    void
    File::removexattr(std::string const& k)
    {
      Node::removexattr(k);
    }

    File::File(FileSystem& owner,
             Address address,
             std::shared_ptr<FileData> data,
             std::shared_ptr<DirectoryData> parent,
             std::string const& name)
      : Node(owner, address, parent, name)
      , _filedata(data)
    {}

    File::~File()
    {
      ELLE_DEBUG("%s: destroyed", *this);
    }

    bool
    File::allow_cache()
    {
      return false;
    }

    void
    File::statfs(struct statvfs * st)
    {
      memset(st, 0, sizeof(struct statvfs));
      st->f_bsize = 32768;
      st->f_frsize = 32768;
      st->f_blocks = 1000000;
      st->f_bavail = 1000000;
      st->f_fsid = 1;
    }

    void
    File::_fetch()
    {
      if (this->_filedata)
        return;
      this->_first_block = std::dynamic_pointer_cast<ACLBlock>(
        this->_owner.fetch_or_die(_address, {}, this->full_path()));

      elle::SafeFinally remove_undecoded_first_block([&] {
          this->_first_block.reset();
      });
      auto perms = get_permissions(*_owner.block_store(), *_first_block);
      _filedata = std::make_shared<FileData>(_parent->_path / _name, *_first_block, perms,
        _owner.block_size().value_or(default_block_size));
      remove_undecoded_first_block.abort();
    }

    FileData::FileData(bfs::path path,
                       Block& block, std::pair<bool, bool> perms,
                       int block_size)
      : _address(block.address())
      , _path(path)
    {
      update(block, perms, block_size);
      _last_used = FileSystem::now();
    }

    void
    FileData::update(Block& block, std::pair<bool, bool> perms, int block_size)
    {
      ELLE_TRACE_SCOPE("%s: update from %f (version: %s, worldperm: %s)",
                       this, block.address(),
                       dynamic_cast<ACLBlock&>(block).version(),
                       dynamic_cast<ACLBlock&>(block).get_world_permissions());
      bool empty;
      elle::IOStream is(
        umbrella([&] {
            auto& d = block.data();
            ELLE_DUMP("block data: %s", d);
            empty = d.empty();
            return d.istreambuf();
          }, EACCES));
      if (empty)
      {
        ELLE_DEBUG("file block is empty");
        auto now = time(nullptr);
        _header = FileHeader(0, 1, S_IFREG | 0600,
                             now, now, now, now,
                             block_size);
      }
      else
      {
        elle::serialization::binary::SerializerIn input(is);
        try
        {
          _fat.clear();
          _header.xattrs.clear();
          input.serialize("header", _header);
          input.serialize("fat", _fat);
          input.serialize("data", _data);
        }
        catch(elle::serialization::Error const& e)
        {
          ELLE_WARN("File deserialization error: %s", e);
          throw rfs::Error(EIO, e.what());
        }
        _header.mode &= ~0606;
        auto& ablock = dynamic_cast<ACLBlock&>(block);
        auto wp = ablock.get_world_permissions();
        if (wp.first)
          _header.mode |= 4;
        if (wp.second)
          _header.mode |= 2;
        if (perms.first)
          _header.mode |= 0400;
        if (perms.second)
          _header.mode |= 0200;
      }
      this->_block_version = dynamic_cast<ACLBlock&>(block).version();
      ELLE_DEBUG("%s: updated from %f: sz=%s, links=%s, mode=%s, fatsize=%s, firstblocksize=%s",
                 this, _address, _header.size, _header.links, print_mode(_header.mode),
                 _fat.size(), _data.size());
    }

    FileData::FileData(bfs::path path, Address address, int mode, int block_size)
    {
      _path = path;
      _address = address;
      _block_version = -1;
      _last_used = FileSystem::now();
      _header = FileHeader ( 0, 1, S_IFREG | mode,
        time(nullptr), time(nullptr), time(nullptr), time(nullptr),
        block_size
      );
    }

    void
    FileData::merge(const FileData& previous, WriteTarget target)
    {
      if (! (target & WriteTarget::perms))
      {
        ELLE_DUMP("overwriting perms");
        _header.mode = previous._header.mode;
        _header.uid = previous._header.uid;
        _header.gid = previous._header.gid;
      }
      if (! (target & WriteTarget::links))
      {
        ELLE_DUMP("overwriting hardlinks");
        _header.links = previous._header.links;
      }
      if (! (target & WriteTarget::data))
      {
        ELLE_DUMP("overwriting data");
        _header.size = previous._header.size;
        if (previous._header.block_size)
          _header.block_size = previous._header.block_size;
        _fat = previous._fat;
        _data = elle::Buffer(previous._data.contents(), previous._data.size());
      }
      if (! (target & WriteTarget::times))
      {
        ELLE_DUMP("overwriting times");
        _header.atime = previous._header.atime;
        _header.mtime = previous._header.mtime;
        _header.ctime = previous._header.ctime;
      }
      if (! (target & WriteTarget::xattrs))
      {
        ELLE_DUMP("overwriting xattrs");
        _header.xattrs = previous._header.xattrs;
      }
      if (! (target & WriteTarget::symlink))
      {
        ELLE_DUMP("overwriting links");
        _header.symlink_target = previous._header.symlink_target;
      }
      if (!_header.block_size)
        _header.block_size = previous._header.block_size;
    }

    void
    FileData::write(FileSystem& fs,
                    WriteTarget target,
                    std::unique_ptr<ACLBlock>& block_, bool first_write)
    {
      ELLE_DEBUG("%s: write at %f: sz=%s, links=%s, mode=%s, fatsize=%s, firstblocksize=%s",
                 this, _address,
                 _header.size, _header.links, print_mode(_header.mode), _fat.size(), _data.size());
      auto& model = *fs.block_store();
      std::unique_ptr<ACLBlock> myblock_;
      auto& block = (&block_ == &DirectoryData::null_block) ? myblock_ : block_;
      bool block_allocated = !block;
      if (!block)
      {
        try
        {
          block = elle::cast<ACLBlock>::runtime(model.fetch(_address));
        }
        catch (model::MissingBlock const&)
        {
          ELLE_WARN("%s: unable to commit as file was deleted", this);
          return;
        }
      }
      ELLE_ASSERT(block);
      if (!block->data().empty())
      {
        FileData previous(_path, *block, {true, true}, 0);
        ELLE_ASSERT(previous.header().block_size);
        merge(previous, target);
        ELLE_DEBUG("%s: post-merge write %f: sz=%s, links=%s, mode=%s fatsize=%s, worldperm=%s version=%s", this, _address,
                   _header.size, _header.links, print_mode(_header.mode), _fat.size(), block->get_world_permissions(),
                   block->version());
      }
      elle::Buffer serdata;
      {
        elle::IOStream os(serdata.ostreambuf());
        auto version = model.version();
        auto versions =
          elle::serialization::_details::dependencies<typename FileData::serialization_tag>(
            version, 42);
        versions.emplace(
          elle::type_info<typename FileData::serialization_tag>(),
          version);
        elle::serialization::binary::SerializerOut output(os,
          versions, true);
        output.serialize("header", _header);
        output.serialize("fat", _fat);
        output.serialize("data", _data);
      }
      try
      {
        ELLE_ASSERT(block);
        block->data(serdata);
        auto resolver =
          std::make_unique<FileConflictResolver>(this->_path, &model, target);
        if (block_allocated)
        {
          if (first_write)
            model.insert(std::move(block), std::move(resolver));
          else
            model.update(std::move(block), std::move(resolver));
        }
        else
        {
          if (first_write)
            model.seal_and_insert(*block, std::move(resolver));
          else
            model.seal_and_update(*block, std::move(resolver));
        }
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        // We made changes to filedata that couldn't be pushed, evict from cache
        fs._file_cache.erase(_address);
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch (model::MissingBlock const&)
      {
        ELLE_WARN("%s: unable to commit as file was deleted", this);
        return;
      }
      catch(elle::Error const& e)
      {
        ELLE_WARN("unexpected exception storing %f: %s",
          _address, e);
        throw rfs::Error(EIO, e.what());
      }
    }

    void
    File::_commit(WriteTarget target)
    {
      ELLE_ASSERT(this->_filedata);
      _filedata->write(_owner, target, _first_block);
    }

    void
    File::_ensure_first_block()
    {
      if (this->_first_block)
        return;
      this->_filedata.reset();
     _fetch();
    }

    void
    File::link(bfs::path const& where)
    {
      std::string newname = where.filename().string();
      bfs::path newpath = where.parent_path();
      auto dir = std::dynamic_pointer_cast<Directory>(
        _owner.filesystem()->path(newpath.string()));
      if (dir->_data->_files.find(newname) != dir->_data->_files.end())
        throw rfs::Error(EEXIST, "target file exists");
      // we need a place to store the link count
      _ensure_first_block();
      _filedata->_header.links++;
      dir->_data->_files.insert(std::make_pair(newname, _parent->_files.at(_name)));
      dir->_data->write(
        _owner,
        {OperationType::insert, newname, EntryType::file,
        _parent->_files.at(_name).second},
        DirectoryData::null_block,
        true);
      _commit(WriteTarget::links);
    }

    void
    File::unlink()
    {
      _ensure_first_block();
      if ( !(_filedata->_header.mode & 0200)
        || !(_parent->_header.mode & 0200))
        THROW_ACCES();
      auto info = _parent->_files.at(_name);
      elle::SafeFinally revert([&] { _parent->_files[_name] = info;});
      _parent->_files.erase(_name);
      _parent->write(
        _owner,
        {OperationType::remove, _name},
        DirectoryData::null_block,
        true);
      revert.abort();
      int links = _filedata->_header.links;
      if (links > 1)
      {
        ELLE_DEBUG("%s remaining links", links - 1);
        _filedata->_header.links--;
        _commit(WriteTarget::links);
      }
      // FIXME: Improves POSIX compatibility but adds window for a data leak.
      else
      {
        ELLE_DEBUG("No remaining links");
        auto it = this->_owner.file_buffers().find(this->_filedata->address());
        if (it != this->_owner.file_buffers().end())
        {
          if (auto file_buffer = it->second.lock())
            file_buffer->_remove_data = true;
        }
        else
        {
          for (unsigned i=0; i<_filedata->_fat.size(); ++i)
          {
            ELLE_DEBUG_SCOPE("removing %s: %f", i, _filedata->_fat[i].first);
            _owner.unchecked_remove(_filedata->_fat[i].first);
          }
          ELLE_DEBUG_SCOPE("removing first block at %f", _first_block->address());
          _owner.unchecked_remove(_first_block->address());
        }
      }
    }

    void
    File::rename(bfs::path const& where)
    {
      ELLE_TRACE_SCOPE("%s: rename to %s", *this, where);
      Node::rename(where);
    }

    void
    File::stat(struct stat* st)
    {
      static elle::Bench bench("bench.file.stat", 10000s);
      elle::Bench::BenchScope bs(bench);
      ELLE_TRACE_SCOPE("%s: stat, parent %s", *this, _parent);
      memset(st, 0, sizeof(struct stat));
      st->st_mode = S_IFREG;
      try
      {
        this->_fetch();
        Node::stat(st);
        st->st_mode |= S_IFREG;
        if ((st->st_mode & 0400) && (_filedata->_header.mode & 0100))
          st->st_mode |= 0100;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_DEBUG("%s: permission exception dropped for stat: %s", *this, e);
      }
      catch (rfs::Error const& e)
      {
        ELLE_DEBUG("%s: filesystem exception: %s", *this, e.what());
        if (e.error_code() != EACCES)
          throw;
      }
      catch (elle::Error const& e)
      {
        ELLE_WARN("unexpected exception on stat: %s", e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      auto it = _owner.file_buffers().find(_filedata->address());
      if (it != _owner.file_buffers().end())
        if (auto fh = it->second.lock())
        {
          ELLE_DEBUG("open file size overwrite: %s -> %s",
                     st->st_size, fh->_file._header.size);
          st->st_size = fh->_file._header.size;
        }
    }

    void
    File::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    struct NewBlockResolver
      : public model::DummyConflictResolver
    {
      using Super = infinit::model::DummyConflictResolver;
      NewBlockResolver(std::string const& name,
                    Address const address)
        : Super()
        , _name(name)
        , _address(address)
      {}

      NewBlockResolver(elle::serialization::Serializer& s,
                    elle::Version const& version)
        : Super() // Do not call Super(s, version)
      {
        this->serialize(s, version);
      }

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override
      {
        Super::serialize(s, version);
        s.serialize("name", this->_name);
        s.serialize("address", this->_address);
      }

      std::string
      description() const override
      {
        return elle::sprintf("insert new block (%f) for file %s",
                             this->_address, this->_name);
      }

      ELLE_ATTRIBUTE(std::string, name);
      ELLE_ATTRIBUTE(Address, address);
    };

    namespace
    {
      const SerializationRegister<NewBlockResolver>
      _register_nbr("NewBlockResolver");
    }

    void
    File::truncate(off_t new_size)
    {
      _fetch();
      ELLE_TRACE("%s: truncate %s -> %s", *this, _filedata->_header.size, new_size);
      if (new_size == signed(_filedata->_header.size))
        return;
      if (new_size > signed(_filedata->_header.size))
      {
        auto h = open(O_RDWR, 0666);
        h->ftruncate(new_size);
        h->close();
        return;
      }
      uint64_t first_block_size = _filedata->_data.size();
      // Remove fat blocks starting from the end
      for (int i = _filedata->_fat.size()-1; i >= 0; --i)
      {
        auto offset = first_block_size + i * _filedata->_header.block_size;
        ELLE_DEBUG("considering %s: [%s, %s]", i,
          offset, offset + _filedata->_header.block_size);
        if (signed(offset) >= new_size)
        {
          // kick the block
          ELLE_DEBUG("removing %f", _filedata->_fat[i].first);
          if (_filedata->_fat[i].first != Address::null)
            unchecked_remove_chb(*_owner.block_store(), _filedata->_fat[i].first, _address);
          _filedata->_fat.pop_back();
        }
        else if (signed(offset + _filedata->_header.block_size) >= new_size)
        {
          // maybe truncate the block
          ELLE_DEBUG("truncating %f", _filedata->_fat[i].first);
          if (_filedata->_fat[i].first == Address::null)
            continue;
          auto targetsize = new_size - offset;
          elle::cryptography::SecretKey sk(_filedata->_fat[i].second);
          auto block = _owner.fetch_or_die(_filedata->_fat[i].first, {}, this->full_path());
          elle::Buffer buf(sk.decipher(block->data()));
          if (buf.size() > targetsize)
            buf.size(targetsize);
          auto newblock = _owner.block_store()->make_block<ImmutableBlock>(
            sk.encipher(buf), _address);
          unchecked_remove_chb(*_owner.block_store(), _filedata->_fat[i].first, this->_address);
          _filedata->_fat[i].first = newblock->address();
          this->_owner.store_or_die(
            std::move(newblock), true,
            std::make_unique<NewBlockResolver>(this->_name, this->_address));
        }
      }
      // check first block data
      if (new_size < signed(_filedata->_data.size()))
        _filedata->_data.size(new_size);
      this->_filedata->_header.size = new_size;
      this->_commit(WriteTarget::data);

      // propagate to all opened file handles
      auto it = _owner.file_buffers().find(_filedata->address());
      if (it != _owner.file_buffers().end())
        if (auto fh = it->second.lock())
        {
          bool dirty = fh->_fat_changed;
          if (!dirty)
          {
            for (auto const& b: fh->_blocks)
            {
              if (b.second.dirty && (b.first +1) * fh->_file._header.size < unsigned(new_size))
              {
                dirty = true;
                break;
              }
            }
          }
          if (dirty)
          {
#ifdef INFINIT_WINDOWS
            // Propagating to dirty open files breaks saving of office documents.
            return;
#else
            ELLE_WARN("Propagating truncate(%s) of %s to open dirty file handle with size %s",
                      new_size, _name, fh->_file._header.size);
#endif
          }
          if (dirty || new_size)
            fh->ftruncate(nullptr, new_size);
          else
          { // No need to go through block removal again
            fh->_file._fat.clear();
            fh->_file._data.reset();
            fh->_file._header.size = 0;
            fh->_blocks.clear();
          }
        }
    }

    std::unique_ptr<rfs::Handle>
    File::open(int flags, mode_t mode)
    {
      // FIXME atime
      ELLE_TRACE_SCOPE("%s: open", *this);
      bool needw = (flags & O_ACCMODE) != O_RDONLY;
      bool needr = (flags & O_ACCMODE) != O_WRONLY;
      if (_owner.read_only() && needw)
        throw rfs::Error(EACCES, "Access denied.");
      if (flags & O_TRUNC)
      {
        truncate(0);
        _fetch();
      }
      else
      {
        _fetch();
        auto mode = this->_filedata->header().mode;
        if ( (needr && !(mode & 0400) && !(mode & 0004)) || (needw && !(mode & 0200) && !(mode & 0002)))
          throw rfs::Error(EACCES, "Access denied.");
      }
      return umbrella([&] {
        return std::unique_ptr<rfs::Handle>(
          new FileHandle(_owner, *_filedata, false));
      });
    }

    std::unique_ptr<rfs::Handle>
    File::create(int flags, mode_t mode)
    {
      if (flags & O_EXCL)
        THROW_EXIST();
      if (flags & O_TRUNC)
        truncate(0);
      _fetch();
      //ELLE_DEBUG("Forcing entry %s", full_path());
      return std::unique_ptr<rfs::Handle>(
        new FileHandle(_owner, *_filedata, true));
    }

    model::blocks::ACLBlock*
    File::_header_block(bool)
    {
      _ensure_first_block();
      return dynamic_cast<model::blocks::ACLBlock*>(_first_block.get());
    }

    FileHeader&
    File::_header()
    {
      if (!_filedata)
        _fetch();
      return _filedata->_header;
    }

    std::vector<std::string>
    File::listxattr()
    {
      _fetch();
      ELLE_TRACE("file listxattr");
      return elle::make_vector(_filedata->_header.xattrs,
                               [](auto const& a)
                               {
                                 return a.first;
                               });
    }

    std::string
    File::getxattr(std::string const& key)
    {
      return umbrella(
        [this, &key]
        {
          if (auto special = xattr_special(key))
          {
            if (*special == "fat")
            {
              this->_fetch();
              auto const fat
                = elle::make_vector(this->_filedata->_fat,
                                    [](auto const& entry)
                                    {
                                      return elle::sprintf("%x", entry.first);
                                    });
              return elle::serialization::json::serialize(fat, false).string();
            }
            else if (*special == "auth")
            {
              this->_ensure_first_block();
              return this->perms_to_json(
                dynamic_cast<ACLBlock&>(*this->_first_block));
            }
          }
          return this->Node::getxattr(key);
        });
    }

    void
    File::setxattr(std::string const& name, std::string const& value, int flags)
    {
      ELLE_TRACE("file setxattr %s", name);
      if (auto special = xattr_special(name))
      {
        ELLE_DEBUG("found special %s", *special);
        if (*special == "fsck.nullentry")
        {
          _fetch();
          int idx = std::stoi(value);
          _filedata->_fat[idx] = std::make_pair(model::Address::null, "");
          _commit(WriteTarget::data);
          return;
        }
      }
      Node::setxattr(name, value, flags);
    }

    std::shared_ptr<rfs::Path>
    File::child(std::string const& name)
    {
      // FIXME remove
      auto const attr_key = "$xattr.";
      if (boost::starts_with(name, attr_key))
        return std::make_shared<XAttributeFile>(shared_from_this(),
          name.substr(strlen(attr_key)));
      else
        THROW_NOTDIR();
    }

    void
    File::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "File(\"%s\")", this->_name);
    }

    const
    unsigned long
    File::default_block_size = 1024 * 1024;
  }
}
