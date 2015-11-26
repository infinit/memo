#include <infinit/filesystem/File.hh>

#include <cryptography/random.hh>
#include <cryptography/SecretKey.hh>

#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/umbrella.hh>
#include <infinit/filesystem/xattribute.hh>

#include <elle/cast.hh>
#include <elle/os/environ.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>

#ifdef INFINIT_WINDOWS
#include <fcntl.h>
#endif

#include <sys/stat.h> // S_IMFT...

#ifdef INFINIT_WINDOWS
#undef stat
#endif

ELLE_LOG_COMPONENT("infinit.filesystem.File");

namespace infinit
{
  namespace filesystem
  {
    static std::unique_ptr<Block>
    resolve_file_conflict(Block& b, model::StoreMode store_mode,
                          boost::filesystem::path p,
                          infinit::model::Model const& m)
    {
      ELLE_LOG_SCOPE(
        "conflict: the file \"%s\" was modified since last read. Your"
        " changes will overwrite previous modifications", p);
      auto block = elle::cast<MutableBlock>::runtime(m.fetch(b.address()));
      block->data(b.data());
      return elle::cast<Block>::runtime(block);
    }

    class FileConflictResolver
      : public model::ConflictResolver
    {
    public:
      FileConflictResolver(elle::serialization::SerializerIn& s)
      {
        serialize(s);
      }
      FileConflictResolver()
      : _model(nullptr)
      {
      }
      FileConflictResolver(boost::filesystem::path path, model::Model* model)
      : _path(path)
      , _model(model)
      {}
      std::unique_ptr<Block> operator()(Block& b, model::StoreMode store_mode) override
      {
        return resolve_file_conflict(b, store_mode, _path, *_model);
      }
      void serialize(elle::serialization::Serializer& s) override
      {
        std::string spath = _path.string();
        s.serialize("path", spath);
        _path = spath;
        if (s.in())
        {
          infinit::model::Model* model = nullptr;
          const_cast<elle::serialization::Context&>(s.context()).get(model);
          ELLE_ASSERT(model);
          _model = model;
        }
      }
      boost::filesystem::path _path;
      model::Model* _model;
      typedef infinit::serialization_tag serialization_tag;
    };
    static const elle::serialization::Hierarchy<model::ConflictResolver>::
    Register<FileConflictResolver> _register_fcr("fcr");

    static std::string perms_to_json(ACLBlock& block)
    {
      auto perms = block.list_permissions();
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
      Node::chmod(mode);
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

    File::File(DirectoryPtr parent, FileSystem& owner, std::string const& name,
               std::unique_ptr<MutableBlock> block)
      : Node(owner, parent, name)
      , _first_block(std::move(block))
      , _first_block_new(false)
      , _r_handle_count(0)
      , _rw_handle_count(0)
      , _fat_changed(false)
      , _full_path(full_path())
    {}

    File::~File()
    {
      ELLE_DEBUG("%s: destroyed", *this);
    }

    bool
    File::allow_cache()
    {
      return true;
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
      if (_rw_handle_count)
        return;
      _parent->_fetch();
      auto it = _parent->_files.find(_name);
      if (it == _parent->_files.end())
      {
        THROW_NOENT;
      }
      Address addr = it->second.second;
      if (!_first_block)
        _first_block = elle::cast<MutableBlock>::runtime(
          _owner.fetch_or_die(addr));
      else
      {
        auto res = elle::cast<MutableBlock>::runtime(
          _owner.fetch_or_die(addr, _first_block->version()));
        if (res)
          _first_block = std::move(res);
        else
          return;
      }
      bool empty = false;
      elle::SafeFinally remove_undecoded_first_block([&] {
          _first_block.reset();
      });
      elle::IOStream is(
        umbrella([&] {
            auto& d = _first_block->data();
            ELLE_DUMP("block data: %s", d);
            empty = d.empty();
            return d.istreambuf();
          }, EACCES));
      if (empty)
      {
        ELLE_DEBUG("file block is empty");
        _header = FileHeader(0, 1, S_IFREG | 0666,
                             time(nullptr), time(nullptr), time(nullptr),
                             File::default_block_size);
      }
      elle::serialization::binary::SerializerIn input(is);
      try
      {
        input.serialize("header", _header);
        input.serialize("fat", _fat);
        input.serialize("data", _data);
      }
      catch(elle::serialization::Error const& e)
      {
        ELLE_WARN("File deserialization error: %s", e);
        throw rfs::Error(EIO, e.what());
      }
      remove_undecoded_first_block.abort();
    }

    void
    File::_commit()
    {
      if (_rw_handle_count)
      {
        return;
      }
      _commit_first();
    }

    void
    File::_commit_first()
    {
      elle::Buffer serdata;
      {
        elle::IOStream os(serdata.ostreambuf());
        elle::serialization::binary::SerializerOut output(os);
        output.serialize("header", _header);
        output.serialize("fat", _fat);
        output.serialize("data", _data);
      }
      _first_block->data(serdata);
      try
      {
        _owner.block_store()->store(*_first_block,
          this->_first_block_new ? model::STORE_INSERT : model::STORE_ANY,
          elle::make_unique<FileConflictResolver>(
            full_path(), _owner.block_store().get()));
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch(elle::Error const& e)
      {
        ELLE_WARN("unexpected exception storing %x: %s",
          this->_first_block->address(), e);
        throw rfs::Error(EIO, e.what());
      }
      this->_first_block_new = false;
    }

    void
    File::_ensure_first_block()
    {
      if (this->_first_block)
        return;
     _fetch();
    }

    AnyBlock*
    File::_block_at(int index, bool create)
    {
      ELLE_ASSERT_GTE(index, 0);
      auto it = _blocks.find(index);
      if (it != _blocks.end())
        return &it->second.block;
      if (_fat.size() <= unsigned(index))
      {
        ELLE_TRACE("%s: block_at(%s) out of range", *this, index);
        if (!create)
        {
          return nullptr;
        }
        _fat.resize(index+1, FatEntry(Address::null, {}));
      }
      AnyBlock b;
      bool is_new = false;
      if (_fat[index].first == Address::null)
      {
        b = AnyBlock(_owner.block_store()->make_block<ImmutableBlock>());
        is_new = true;
      }
      else
      {
        b = AnyBlock(_owner.fetch_or_die(_fat[index].first), _fat[index].second);
        is_new = false;
      }

      auto inserted = _blocks.insert(std::make_pair(index,
        File::CacheEntry{AnyBlock(std::move(b)), false}));
      inserted.first->second.last_use = std::chrono::system_clock::now();
      inserted.first->second.dirty = false; // we just fetched or inserted it
      inserted.first->second.new_block = is_new;
      return &inserted.first->second.block;
    }

    void
    File::link(boost::filesystem::path const& where)
    {
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();
      auto dir = std::dynamic_pointer_cast<Directory>(
        _owner.filesystem()->path(newpath.string()));
      if (dir->_files.find(newname) != dir->_files.end())
        throw rfs::Error(EEXIST, "target file exists");
      // we need a place to store the link count
      _ensure_first_block();
      _header.links++;
      dir->_files.insert(std::make_pair(newname, _parent->_files.at(_name)));
      dir->_commit({OperationType::insert, newname}, true);
      _owner.filesystem()->extract(where.string());
      _commit();
    }

    void
    File::unlink()
    {
      ELLE_TRACE_SCOPE("%s: unlink, handle_count %s,%s", *this,
        _r_handle_count, _rw_handle_count);

      if (_parent)
        _fetch();

      // multi method can't be called after deletion from parent
      if (!_first_block)
      {
        if (!_parent)
        {
          ELLE_ERR("%s: parent is null and root block unavailable", *this);
          _remove_from_cache(_full_path);
          return;
        }
      }
      if (_parent)
      {
        auto info = _parent->_files.at(_name);
        elle::SafeFinally revert([&] { _parent->_files[_name] = info;});
        _parent->_files.erase(_name);
        _parent->_commit({OperationType::remove, _name}, true);
        revert.abort();
        _parent = nullptr;
        _remove_from_cache(_full_path);
      }
      if (_rw_handle_count || _r_handle_count)
        return;
      int links = _header.links;
      if (links > 1)
      {
        ELLE_DEBUG("%s remaining links", links - 1);
        _header.links--;
        _commit_first();
      }
      else
      {
        ELLE_DEBUG("No remaining links");
        for (unsigned i=0; i<_fat.size(); ++i)
        {
          _owner.unchecked_remove(_fat[i].first);
        }
        _owner.unchecked_remove(_first_block->address());
      }
      _remove_from_cache(_full_path);
    }

    void
    File::rename(boost::filesystem::path const& where)
    {
      ELLE_TRACE_SCOPE("%s: rename to %s", *this, where);
      Node::rename(where);
      _full_path = full_path();
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
      if (new_size > signed(_header.size))
      {
        auto h = open(O_RDWR, 0666);
        char buf[16384] = {0};
        int64_t sz = _header.size;
        while (sz < new_size)
        {
          auto nsz = std::min(off_t(16384), new_size - sz);
          sz += h->write(elle::WeakBuffer(buf, nsz), nsz, sz);
        }
        h->close();
        return;
      }
      // Remove fat blocks starting from the end
      for (int i = _fat.size()-1; i >= 0; --i)
      {
        auto offset = first_block_size + i * _header.block_size;
        if (signed(offset) >= new_size)
        { // kick the block
          _owner.unchecked_remove(_fat[i].first);
          _fat.pop_back();
        }
        else if (signed(offset + _header.block_size) >= new_size)
        { // maybe truncate the block
          cryptography::SecretKey sk(_fat[i].second);
          auto targetsize = new_size - offset;
          auto block = _owner.fetch_or_die(_fat[i].first);
          elle::Buffer buf(sk.decipher(block->data()));
          if (buf.size() > targetsize)
          {
            buf.size(targetsize);
          }
          auto newblock = _owner.block_store()->make_block<ImmutableBlock>(
            sk.encipher(buf));
          _owner.unchecked_remove(_fat[i].first);
          _fat[i].first = newblock->address();
          _owner.store_or_die(std::move(newblock));
        }
      }
      // check first block data
      if (new_size < signed(_data.size()))
        _data.size(new_size);
      this->_commit();
    }

    std::unique_ptr<rfs::Handle>
    File::open(int flags, mode_t mode)
    {
      ELLE_TRACE_SCOPE("%s: open", *this);
      bool needw = (flags & O_ACCMODE) != O_RDONLY;
      bool needr = (flags & O_ACCMODE) != O_WRONLY;
      if (flags & O_TRUNC)
        truncate(0);
      else
      { // preemptive  permissions check
        if (!_rw_handle_count)
        {
          _fetch();
        }
        _owner.ensure_permissions(*_first_block.get(), needr, needw);
      }
      _owner.filesystem()->set(full_path().string(), shared_from_this());
      return umbrella([&] {
        return std::unique_ptr<rfs::Handle>(new FileHandle(
          std::dynamic_pointer_cast<File>(shared_from_this()), needw, false, true));
      });
    }

    std::unique_ptr<rfs::Handle>
    File::create(int flags, mode_t mode)
    {
      if (flags & O_TRUNC)
        truncate(0);
      ELLE_DEBUG("Forcing entry %s", full_path());
      if (!_owner.single_mount())
        _owner.filesystem()->set(full_path().string(), shared_from_this());
      return std::unique_ptr<rfs::Handle>(new FileHandle(
        std::dynamic_pointer_cast<File>(shared_from_this()), true));
    }

    void
    File::_commit_all()
    {
      ELLE_TRACE_SCOPE("%s: commit", *this);
      ELLE_DEBUG_SCOPE("%s: push blocks", *this);
      if (!check_cache(0))
      {
        ELLE_DEBUG("store first block: %f with payload %s, fat %s, total_size %s", *this->_first_block,
                   this->_data.size(), this->_fat, _header.size)
        {
          _header.mtime = time(nullptr);
          _commit_first();
        }
      }
    }

    bool
    File::_flush_block(int id)
    {
      bool fat_change = false;
      auto it = _blocks.find(id);
      Address prev = it->second.block.address();
      auto key = cryptography::random::generate<elle::Buffer>(32).string();
      Address addr = it->second.block.crypt_store(*_owner.block_store(),
        it->second.new_block? model::STORE_INSERT : model::STORE_ANY,
        key);
      if (addr != prev)
      {
        ELLE_DEBUG("Changing address of block %s: %s -> %s", it->first,
          prev, addr);
        fat_change = true;
        _fat[id] = FatEntry(addr, key);
        if (!it->second.new_block)
        {
          _owner.unchecked_remove(prev);
        }
      }
      it->second.new_block = false;
      it->second.dirty = false;
      return fat_change;
    }

    bool
    File::check_cache(int cache_size)
    {
      if (cache_size < 0)
        cache_size = max_cache_size;
      typedef std::pair<const int, CacheEntry> Elem;
      if (cache_size == 0)
      {
        // Final flush, wait on all async ops
        while (!_flushers.empty())
        {
          reactor::wait(*_flushers.back());
          _flushers.pop_back();
        }
      }
      else
      {
        // Just wait on finished ops to get exceptions
        for (int i=0; i<signed(_flushers.size()); ++i)
        {
          if (_flushers[i]->done())
          {
            reactor::wait(*_flushers[i]);
            std::swap(_flushers[i], _flushers[_flushers.size()-1]);
            _flushers.pop_back();
            --i;
          }
        }
      }
      while (_blocks.size() > unsigned(cache_size))
      {
        auto it = std::min_element(_blocks.begin(), _blocks.end(),
          [](Elem const& a, Elem const& b) -> bool
          {
            return a.second.last_use < b.second.last_use;
          });
        ELLE_TRACE("Removing block %s from cache", it->first);
        if (cache_size == 0)
        { // final flush, sync
          if (it->second.dirty)
          {
            _fat_changed = _flush_block(it->first) || _fat_changed;
          }
          _blocks.erase(it);
        }
        else
        {
          if (it->second.dirty)
          {
            int id = it->first;
            ELLE_TRACE("starting async flusher for %s", id);
            auto ab = std::make_shared<AnyBlock>(std::move(it->second.block));
            bool new_block = it->second.new_block;
            _flushers.emplace_back(
              new reactor::Thread("flusher", [this, id, ab, new_block] {
                auto key = cryptography::random::generate<elle::Buffer>(32).string();
                auto old_addr = ab->address();
                Address addr = ab->crypt_store(*_owner.block_store(),
                  new_block? model::STORE_INSERT : model::STORE_ANY,
                  key);
                if (addr != old_addr)
                {
                  ELLE_DEBUG("Changing address of block %s: %s -> %s", id,
                    old_addr, addr);
                  _fat_changed = true;
                  _fat[id] = FatEntry(addr, key);
                  if (!new_block)
                  {
                    _owner.unchecked_remove(old_addr);
                  }
                }
            }, reactor::Thread::managed = true));
          }
          _blocks.erase(it);
        }
      }
      bool prev = _fat_changed;
      if (_fat_changed)
      {
        _commit_first();
        _first_block_new = false;
        _fat_changed = false;
      }
      return prev;
    }

    std::vector<std::string> File::listxattr()
    {
      _fetch();
      ELLE_TRACE("file listxattr");
      std::vector<std::string> res;
      for (auto const& a: _header.xattrs)
        res.push_back(a.first);
      return res;
    }

    std::string
    File::getxattr(std::string const& key)
    {
      if (key == "user.infinit.block")
      {
        if (_first_block)
          return elle::sprintf("%x", _first_block->address());
        else
        {
          auto const& elem = _parent->_files.at(_name);
          return elle::sprintf("%x", elem.second);
        }
      }
      else if (key == "user.infinit.fat")
      {
        _fetch();
        std::stringstream res;
        res <<  "total_size: "  << _header.size  << "\n";
        for (int i=0; i < signed(_fat.size()); ++i)
        {
          res << i << ": " << _fat[i].first << "\n";
        }
        return res.str();
      }
      else if (key == "user.infinit.auth")
      {
        Address addr = _parent->_files.at(_name).second;
        auto block = _owner.fetch_or_die(addr);
        return perms_to_json(dynamic_cast<ACLBlock&>(*block));
      }
      else
        return Node::getxattr(key);
    }

    void
    File::setxattr(std::string const& name, std::string const& value, int flags)
    {
      ELLE_TRACE("file setxattr %s", name);
      if (name.find("user.infinit.auth.") == 0)
      {
        set_permissions(name.substr(strlen("user.infinit.auth.")), value,
                        _parent->_files.at(_name).second);
      }
      else
        Node::setxattr(name, value, flags);
    }

    std::shared_ptr<rfs::Path>
    File::child(std::string const& name)
    {
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
  }
}
