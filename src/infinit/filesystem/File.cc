#include <infinit/filesystem/File.hh>

#include <pair>

#ifdef INFINIT_WINDOWS
#include <fcntl.h>
#endif
#include <sys/stat.h> // S_IMFT...

#include <elle/cast.hh>
#include <elle/os/environ.hh>

#include <cryptography/random.hh>
#include <cryptography/SecretKey.hh>

#ifdef INFINIT_WINDOWS
#undef stat
#endif

#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/umbrella.hh>
#include <infinit/filesystem/xattribute.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>

ELLE_LOG_COMPONENT("infinit.filesystem.File");

namespace infinit
{
  namespace filesystem
  {

    static std::string print_mode(int m)
    {
      std::string res;
      res += (m & 0200) ? 'r' : '-';
      res += (m & 0400) ? 'w' : '-';
      res += (m & 0100) ? 'x' : '-';
      res += (m & 0020) ? 'r' : '-';
      res += (m & 0040) ? 'w' : '-';
      res += (m & 0010) ? 'x' : '-';
      res += (m & 0002) ? 'r' : '-';
      res += (m & 0004) ? 'w' : '-';
      res += (m & 0001) ? 'x' : '-';
      return res;
    }

    FileConflictResolver::FileConflictResolver(elle::serialization::SerializerIn& s,
                                               elle::Version const& v)
    {
      this->serialize(s, v);
    }

    FileConflictResolver::FileConflictResolver()
      : _model(nullptr)
    {}

    FileConflictResolver::FileConflictResolver(boost::filesystem::path path, model::Model* model,
                         WriteTarget target)
      : _path(path)
      , _model(model)
      , _target(target)
    {}

    std::unique_ptr<Block>
    FileConflictResolver::operator()(Block& b,
                                     Block& current,
                                     model::StoreMode store_mode)
    {
      ELLE_LOG_SCOPE(
        "conflict: the file \"%s\" was modified since last read. Your"
        " changes will overwrite previous modifications", this->_path);
      FileData cd(_path, current, {true, true});
      FileData od(_path, b, {true, true});
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
      { // acl permission changes are handled by a different resolver
        auto perms = dynamic_cast<ACLBlock&>(b).get_world_permissions();
        dynamic_cast<ACLBlock&>(*block).set_world_permissions(perms.first, perms.second);
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
        infinit::model::Model* model = nullptr;
        const_cast<elle::serialization::Context&>(s.context()).get(model);
        ELLE_ASSERT(model);
        this->_model = model;
      }
    }


    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<FileConflictResolver> _register_fcr("fcr");

    static std::string perms_to_json(model::Model& model, ACLBlock& block)
    {
      auto perms = block.list_permissions(model);
      elle::json::Array v;
      for (auto const& perm: perms)
      {
        elle::json::Object o;
        o["name"] = perm.user->name();
        o["read"] = perm.read;
        o["write"] = perm.write;
        v.push_back(o);
      }
      std::stringstream ss;
      elle::json::write(ss, v, true);
      return ss.str();
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
        this->_owner.fetch_or_die(_address, {}, this));
      
      elle::SafeFinally remove_undecoded_first_block([&] {
          this->_first_block.reset();
      });
      auto perms = _owner.get_permissions(*_first_block);
      _filedata = std::make_shared<FileData>(_parent->_path / _name, *_first_block, perms);
      remove_undecoded_first_block.abort();
    }

    FileData::FileData(boost::filesystem::path path,
                       Block& block, std::pair<bool, bool> perms)
    : _address(block.address())
    , _path(path)
    {
      update(block, perms);
      _last_used = FileSystem::now();
    }
    void
    FileData::update(Block& block, std::pair<bool, bool> perms)
    {
      ELLE_DEBUG("%s: updating from %f ver=%s, worldperm=%s",
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
        _header = FileHeader(0, 1, S_IFREG | 0600,
                             time(nullptr), time(nullptr), time(nullptr),
                             File::default_block_size);
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

    FileData::FileData(boost::filesystem::path path, Address address, int mode)
    {
      _path = path;
      _address = address;
      _block_version = -1;
      _last_used = FileSystem::now();
      _header = FileHeader ( 0, 1, S_IFREG | mode,
        time(nullptr), time(nullptr), time(nullptr),
        File::default_block_size
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
    }

    void
    FileData::write(model::Model& model,
                    WriteTarget target,
                    std::unique_ptr<ACLBlock>& block, bool first_write)
    {
      ELLE_DEBUG("%s: write at %f: sz=%s, links=%s, mode=%s, fatsize=%s, firstblocksize=%s",
                 this, _address,
                 _header.size, _header.links, print_mode(_header.mode), _fat.size(), _data.size());
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
      if (!block->data().empty())
      {
        FileData previous(_path, *block, {true, true});
        merge(previous, target);
        ELLE_DEBUG("%s: post-merge write %f: sz=%s, links=%s, mode=%s fatsize=%s, worldperm=%s version=%s", this, _address,
                   _header.size, _header.links, print_mode(_header.mode), _fat.size(), block->get_world_permissions(),
                   block->version());
      }
      elle::Buffer serdata;
      {
        elle::IOStream os(serdata.ostreambuf());
        elle::serialization::binary::SerializerOut output(os);
        output.serialize("header", _header);
        output.serialize("fat", _fat);
        output.serialize("data", _data);
      }
      try
      {
        block->data(serdata);
        if (!block_allocated)
        {
          model.store(*block,
                      first_write ? model::STORE_INSERT : model::STORE_UPDATE,
                      elle::make_unique<FileConflictResolver>(
                        _path, &model, target));
        }
        else
        {
          model.store(std::move(block),
                      first_write ? model::STORE_INSERT : model::STORE_UPDATE,
                      elle::make_unique<FileConflictResolver>(
                        _path, &model, target));
        }
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
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
      _filedata->write(*_owner.block_store(), target, _first_block);
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
    File::link(boost::filesystem::path const& where)
    {
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();
      auto dir = std::dynamic_pointer_cast<Directory>(
        _owner.filesystem()->path(newpath.string()));
      if (dir->_data->_files.find(newname) != dir->_data->_files.end())
        throw rfs::Error(EEXIST, "target file exists");
      // we need a place to store the link count
      _ensure_first_block();
      _filedata->_header.links++;
      dir->_data->_files.insert(std::make_pair(newname, _parent->_files.at(_name)));
      dir->_data->write(
        *_owner.block_store(),
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
      auto info = _parent->_files.at(_name);
      elle::SafeFinally revert([&] { _parent->_files[_name] = info;});
      _parent->_files.erase(_name);
      _parent->write(
        *_owner.block_store(),
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
      else
      {
        ELLE_DEBUG_SCOPE("No remaining links");
        // FIXME optimize pass removal data
        for (unsigned i=0; i<_filedata->_fat.size(); ++i)
        {
          ELLE_DEBUG_SCOPE("removing %s: %f", i, _filedata->_fat[i].first);
          _owner.unchecked_remove(_filedata->_fat[i].first);
        }
        ELLE_DEBUG_SCOPE("removing first block at %f", _first_block->address());
        _owner.unchecked_remove(_first_block->address());
      }
    }

    void
    File::rename(boost::filesystem::path const& where)
    {
      ELLE_TRACE_SCOPE("%s: rename to %s", *this, where);
      Node::rename(where);
    }

    void
    File::stat(struct stat* st)
    {
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
        {
          throw;
        }
      }
      catch (elle::Error const& e)
      {
        ELLE_WARN("unexpected exception on stat: %s", e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      auto it = this->_size_map.find(_filedata->address());
      if (it != this->_size_map.end())
      {
        ELLE_DEBUG("open file size overwrite: %s -> %s",
                   st->st_size, it->second.first);
        st->st_size = it->second.first;
      }
      else
        ELLE_DEBUG("stat size: %s", st->st_size);
    }

    void
    File::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    void
    File::truncate(off_t new_size)
    {
      _fetch();
      elle::SafeFinally write_cache_size([&] {
          auto it = _size_map.find(this->_address);
          if (it != _size_map.end())
            it->second.first = new_size;
      });
      ELLE_TRACE("%s: truncate %s -> %s", *this, _filedata->_header.size, new_size);
      if (new_size == signed(_filedata->_header.size))
        return;
      if (new_size > signed(_filedata->_header.size))
      {
        auto h = open(O_RDWR, 0666);
        char buf[16384] = {0};
        int64_t sz = _filedata->_header.size;
        while (sz < new_size)
        {
          auto nsz = std::min(off_t(16384), new_size - sz);
          sz += h->write(elle::WeakBuffer(buf, nsz), nsz, sz);
        }
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
        { // kick the block
          ELLE_DEBUG("removing %f", _filedata->_fat[i].first);
          _owner.unchecked_remove(_filedata->_fat[i].first);
          _filedata->_fat.pop_back();
        }
        else if (signed(offset + _filedata->_header.block_size) >= new_size)
        { // maybe truncate the block
          ELLE_DEBUG("truncating %f", _filedata->_fat[i].first);
          auto targetsize = new_size - offset;
          cryptography::SecretKey sk(_filedata->_fat[i].second);
          auto block = _owner.fetch_or_die(_filedata->_fat[i].first);
          elle::Buffer buf(sk.decipher(block->data()));
          if (buf.size() > targetsize)
          {
            buf.size(targetsize);
          }
          auto newblock = _owner.block_store()->make_block<ImmutableBlock>(
            sk.encipher(buf), _address);
          _owner.unchecked_remove(_filedata->_fat[i].first);
          _filedata->_fat[i].first = newblock->address();
          _owner.store_or_die(std::move(newblock), model::STORE_INSERT);
        }
      }
      // check first block data
      if (new_size < signed(_filedata->_data.size()))
        _filedata->_data.size(new_size);
      this->_filedata->_header.size = new_size;
      this->_commit(WriteTarget::data);
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
        _ensure_first_block();
        _owner.ensure_permissions(*_first_block.get(), needr, needw);
      }
      return umbrella([&] {
        return std::unique_ptr<rfs::Handle>(
          new FileHandle(*_owner.block_store(), *_filedata, needw, false, true));
      });
    }

    std::unique_ptr<rfs::Handle>
    File::create(int flags, mode_t mode)
    {
      if (flags & O_TRUNC)
        truncate(0);
      _fetch();
      //ELLE_DEBUG("Forcing entry %s", full_path());
      return std::unique_ptr<rfs::Handle>(
        new FileHandle(*_owner.block_store(), *_filedata, true));
    }

    model::blocks::ACLBlock*
    File::_header_block()
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

    std::vector<std::string> File::listxattr()
    {
      _fetch();
      ELLE_TRACE("file listxattr");
      std::vector<std::string> res;
      for (auto const& a: _filedata->_header.xattrs)
        res.push_back(a.first);
      return res;
    }

    std::string
    File::getxattr(std::string const& key)
    {
      if (auto special = xattr_special(key))
      {
        if (*special == "fat")
        {
          _fetch();
          std::stringstream res;
          res <<  "total_size: "  << _filedata->_header.size  << "\n";
          for (int i=0; i < signed(_filedata->_fat.size()); ++i)
          {
            res << i << ": " << _filedata->_fat[i].first << "\n";
          }
          return res.str();
        }
        else if (*special == "auth")
        {
          _ensure_first_block();
          return perms_to_json(*_owner.block_store(), dynamic_cast<ACLBlock&>(*_first_block));
        }
      }
      return Node::getxattr(key);
    }

    void
    File::setxattr(std::string const& name, std::string const& value, int flags)
    {
      ELLE_TRACE("file setxattr %s", name);
      if (auto special = xattr_special(name))
      {
        ELLE_DEBUG("found special %s", *special);
        if (special->find("auth.") == 0)
        {
          set_permissions(special->substr(strlen("auth.")), value, _address);
          return;
        }
        else if (*special == "fsck.nullentry")
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
      static const char* attr_key = "$xattr.";
      if (name.size() > strlen(attr_key)
        && name.substr(0, strlen(attr_key)) == attr_key)
      {
        return std::make_shared<XAttributeFile>(shared_from_this(),
          name.substr(strlen(attr_key)));
      }
      else
        THROW_NOTDIR;
    }

    void
    File::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "File(\"%s\")", this->_name);
    }

    File::SizeMap File::_size_map;
  }
}
