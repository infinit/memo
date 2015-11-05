#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/umbrella.hh>

#include <elle/cast.hh>
#include <elle/os/environ.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>

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
      , _handle_count(0)
      , _full_path(full_path())
    {}

    File::~File()
    {
      ELLE_DEBUG("%s: destroyed", *this);
    }

    bool
    File::allow_cache()
    {
      return _owner.single_mount() ||  _handle_count > 0;
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

    bool
    File::_multi()
    {
      ELLE_ASSERT(!!_first_block);
      Header h = _header();
      return !h.is_bare;
    }

    void
    File::_ensure_first_block()
    {
      if (_first_block)
        return;
      Address addr = _parent->_files.at(_name).address;
      _first_block = elle::cast<MutableBlock>::runtime(
        _owner.fetch_or_die(addr));
      umbrella([&] {
          this->_block_cache = _first_block->cache_update(std::move(this->_block_cache));
      });
    }

    File::Header
    File::_header()
    {
      _ensure_first_block();
      Header res;
      uint32_t v;
      memcpy(&v, _first_block->data().mutable_contents(), 4);
      res.version = ntohl(v);
      res.is_bare = res.version & 0xFF000000;
      res.version = res.version & 0x00FFFFFF;
      memcpy(&v, _first_block->data().mutable_contents()+4, 4);
      res.block_size = ntohl(v);
      memcpy(&v, _first_block->data().mutable_contents()+8, 4);
      res.links = ntohl(v);
      uint64_t v2;
      memcpy(&v2, _first_block->data().mutable_contents()+12, 8);
      res.total_size = ((uint64_t)ntohl(v2)<<32) + ntohl(v2 >> 32);
      return res;
      ELLE_DEBUG("Header: bs=%s links=%s size=%s", res.block_size, res.links, res.total_size);
    }

    void
    File::_header(Header const& h)
    {
      _first_block->data([&](elle::Buffer& data) {
          if (data.size() < unsigned(header_size))
            data.size(header_size);
          uint32_t v;
          v = htonl(h.current_version | (h.is_bare ? 0x01000000 : 0));
          memcpy(data.mutable_contents(), &v, 4);
          v = htonl(h.block_size);
          memcpy(data.mutable_contents()+4, &v, 4);
          v = htonl(h.links);
          memcpy(data.mutable_contents()+8, &v, 4);
          uint64_t v2 = ((uint64_t)htonl(h.total_size)<<32) + htonl(h.total_size >> 32);
          memcpy(data.mutable_contents()+12, &v2, 8);
      });

    }

    AnyBlock*
    File::_block_at(int index, bool create)
    {
      ELLE_ASSERT_GTE(index, 0);
      int offset = (index+1) * sizeof(Address);
      int sz = _first_block->data().size();
      if (sz < offset + signed(sizeof(Address)))
      {
        ELLE_TRACE("%s: block_at(%s) out of range", *this, index);
        if (!create)
        {
          return nullptr;
        }
      }
      char zeros[sizeof(Address)];
      memset(zeros, 0, sizeof(Address));
      AnyBlock b;
      Address addr;
      bool is_new = false;
      if (sz < offset+signed(sizeof(Address)) || !memcmp(zeros, _first_block->data().mutable_contents() + offset,
                 sizeof(Address)))
      { // allocate
        ELLE_TRACE("%s: block_at(%s) is zero", *this, index);
        if (!create)
        {
          return nullptr;
        }
        b = AnyBlock(_owner.block_store()->make_block<ImmutableBlock>());
        is_new = true;
      }
      else
      {
        addr = Address(*(Address*)(_first_block->data().mutable_contents() + offset));
        b = AnyBlock(_owner.fetch_or_die(addr));
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
      if (!_first_block)
      {
        _first_block = elle::cast<MutableBlock>::runtime
          (_owner.fetch_or_die(_parent->_files.at(_name).address));
        umbrella([&] {
            this->_block_cache = _first_block->cache_update(std::move(this->_block_cache));
        });
      }
      bool multi = _multi();
      if (!multi)
        _switch_to_multi(false);
      Header header = _header();
      header.links++;
      _header(header);
      dir->_files.insert(std::make_pair(newname, _parent->_files.at(_name)));
      dir->_files.at(newname).name = newname;
      dir->_commit({OperationType::insert, newname}, true);
      _owner.filesystem()->extract(where.string());
      _commit();
    }

    void
    File::unlink()
    {
      ELLE_TRACE_SCOPE("%s: unlink, handle_count %s", *this, _handle_count);
      static bool no_unlink = !elle::os::getenv("INHIBIT_UNLINK", "").empty();
      if (no_unlink)
      { // DEBUG: link the file in root directory
        std::string n("__" + full_path().string());
        for (unsigned int i=0; i<n.length(); ++i)
          if (n[i] == '/')
          n[i] = '_';
        DirectoryPtr dir = _parent;
        while (dir->_parent)
          dir = dir->_parent;
        auto& cur = _parent->_files.at(_name);
        dir->_files.insert(std::make_pair(n, cur));
        dir->_files.at(n).name = n;
      }
      // multi method can't be called after deletion from parent
      if (!_first_block)
      {
        if (!_parent)
        {
          ELLE_ERR("%s: parent is null and root block unavailable", *this);
          _remove_from_cache(_full_path);
          return;
        }
        _first_block = elle::cast<MutableBlock>::runtime
          (_owner.fetch_or_die(_parent->_files.at(_name).address));
        umbrella([&] {
            this->_block_cache = _first_block->cache_update(std::move(this->_block_cache));
        });
      }
      bool multi = umbrella([&] { return _multi();});
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
      if (_handle_count)
        return;
      if (!multi)
      {
        if (!no_unlink)
        {
          _owner.unchecked_remove(_first_block->address());
          _first_block.reset();
        }
      }
      else
      {
        int links = _header().links;
        if (links > 1)
        {
          ELLE_DEBUG("%s remaining links", links - 1);
          Header header = _header();
          header.links--;
          _header(header);
          _owner.store_or_die(std::move(_first_block));
        }
        else
        {
          ELLE_DEBUG("No remaining links");
          Address::Value zero;
          memset(&zero, 0, sizeof(Address::Value));
          Address* addr = (Address*)(void*)_first_block->data().mutable_contents();
          for (unsigned i=1; i*sizeof(Address) < _first_block->data().size(); ++i)
          {
            if (!memcmp(addr[i].value(), zero, sizeof(Address::Value)))
              continue; // unallocated block
            _owner.block_store()->remove(addr[i]);
          }
          _owner.block_store()->remove(_first_block->address());
        }
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
      Node::stat(st);
      if (_parent)
        st->st_size = _parent->_files.at(_name).size;
      st->st_blocks = st->st_size / 512;
      st->st_nlink = 1;
      mode_t mode = st->st_mode;
      st->st_mode &= ~0777;
      try
      {
        ELLE_DEBUG( (!!_handle_count) ? "block from cache" : "feching block");
        if (!_handle_count && _parent)
        {
          _first_block = elle::cast<MutableBlock>::runtime
            (_owner.fetch_or_die(_parent->_files.at(_name).address));
          umbrella([&] {
              this->_block_cache = _first_block->cache_update(std::move(this->_block_cache));
          });
        }
        if (!_first_block)
        {
          ELLE_WARN("%s: stat on unlinked file", *this);
          return;
        }
        auto h = _header();
        ELLE_DEBUG("%s: overriding in-dir size %s with %s from %s",
                   *this, st->st_size, h.total_size,
                   _first_block->address());
        st->st_size = h.total_size;
        if (!_multi())
        {
          int64_t sz = _first_block->data().size() - header_size;
          if (sz > st->st_size)
          {
            ELLE_DEBUG("%s: overriding size with block size %s", *this, sz);
            st->st_size = sz;
          }
        }
        st->st_blocks = st->st_size / 512;
        st->st_nlink = h.links;
        st->st_mode = mode;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_DEBUG("%s: permission exception dropped for stat: %s", *this, e);
      }
      catch (rfs::Error const&)
      {
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
      if (!_handle_count)
      {
        _first_block = elle::cast<MutableBlock>::runtime
          (_owner.fetch_or_die(_parent->_files.at(_name).address));
        umbrella([&] {
            this->_block_cache = _first_block->cache_update(std::move(this->_block_cache));
        });
      }
      if (!_multi() && new_size > signed(default_block_size))
        _switch_to_multi(true);
      if (!_multi())
      {
        _first_block->data
          ([new_size] (elle::Buffer& data) { data.size(header_size + new_size); });
      }
      else
      {
        Header header = _header();
        if (header.total_size <= unsigned(new_size))
        {
          header.total_size = new_size;
          _header(header);
          _owner.store_or_die(std::move(_first_block));
          return;
        }
        // FIXME: addr should be a Address::Value
        uint32_t block_size = header.block_size;
        Address::Value zero;
        memset(&zero, 0, sizeof(Address::Value));
        Address* addr = (Address*)(void*)_first_block->data().mutable_contents();
        // last block id to keep, always keep block 0
        int drop_from = new_size? (new_size-1) / block_size : 0;
        // addr[0] is our headers
        for (unsigned i=drop_from + 2; i*sizeof(Address) < _first_block->data().size(); ++i)
        {
           if (!memcmp(addr[i].value(), zero, sizeof(Address::Value)))
            continue; // unallocated block
          _owner.block_store()->remove(addr[i]);
          _blocks.erase(i-1);
        }
        _first_block->data(
          [drop_from] (elle::Buffer& data)
          {
            data.size((drop_from + 2) * sizeof(Address));
          });
        // last block surviving the cut might need resizing
        if (drop_from >=0 && !memcmp(addr[drop_from+1].value(), zero, sizeof(Address::Value)))
        {
          AnyBlock* bl;
          auto it = _blocks.find(drop_from);
          if (it != _blocks.end())
          {
            bl = &it->second.block;
            it->second.dirty = true;
          }
          else
          {
            _blocks[drop_from].block =
              AnyBlock(_owner.fetch_or_die(addr[drop_from + 1]));
            CacheEntry& ent = _blocks[drop_from];
            bl = &ent.block;
            ent.dirty = true;
          }
          bl->data([new_size, block_size] (elle::Buffer& data)
                   { data.size(new_size % block_size); });
        }
        header.total_size = new_size;
        _header(header);
        if (new_size <= block_size && header.links == 1)
        { // switch back from multi to direct
          auto& data = _parent->_files.at(_name);
          data.size = new_size;
          _parent->_commit({OperationType::update, _name});
          // Replacing FAT block with first block would be simpler,
          // but it might be immutable
          AnyBlock* data_block;
          auto it = _blocks.find(0);
          if (it != _blocks.end())
            data_block = & it->second.block;
          else
          {
            std::unique_ptr<Block> bl = _owner.fetch_or_die(addr[1]);
            _blocks[0] = {AnyBlock(std::move(bl)), false, {}, false};
            data_block = &_blocks[0].block;
          }
          _owner.block_store()->remove(addr[1]);
          _first_block->data
            ([&] (elle::Buffer& data) {
              data.size(header_size);
              data.append(data_block->data().contents(), new_size);
            });
           _header(Header {true, Header::current_version, default_block_size, 1, (uint64_t)new_size});
          _blocks.clear();
          _owner.store_or_die(*_first_block, infinit::model::STORE_UPDATE);
        }
      }
      this->_commit();
    }

    std::unique_ptr<rfs::Handle>
    File::open(int flags, mode_t mode)
    {
      ELLE_TRACE_SCOPE("%s: open", *this);
      if (flags & O_TRUNC)
        truncate(0);
      else
      { // preemptive  permissions check
        bool needw = (flags & O_ACCMODE) != O_RDONLY;
        bool needr = (flags & O_ACCMODE) != O_WRONLY;

        auto dn =
          std::dynamic_pointer_cast<model::doughnut::Doughnut>(_owner.block_store());
        auto keys = dn->keys();
        Address addr = _parent->_files.at(_name).address;
        if (!_handle_count)
        {
          ELLE_DEBUG("fetch first block")
            this->_first_block = elle::cast<MutableBlock>::runtime(
              this->_owner.fetch_or_die(addr));
          umbrella(
            [&]
            {
              this->_block_cache =
                this->_first_block->cache_update(std::move(this->_block_cache));
            });
        }
        auto acl = dynamic_cast<model::blocks::ACLBlock*>(_first_block.get());
        ELLE_ASSERT(acl);
        umbrella([&] {
            for (auto const& e: acl->list_permissions())
            {
              auto u = dynamic_cast<model::doughnut::User*>(e.user.get());
              if (!u)
                continue;
              if (e.write >= needw && e.read >= needr && u->key() == keys.K())
                return;
            }
            throw rfs::Error(EACCES, "No write permissions.");
        });
      }
      _owner.filesystem()->set(full_path().string(), shared_from_this());
      return umbrella([&] {
        return std::unique_ptr<rfs::Handle>(new FileHandle(
          std::dynamic_pointer_cast<File>(shared_from_this())));
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
        std::dynamic_pointer_cast<File>(shared_from_this())));
    }

    void
    File::_commit()
    {
      ELLE_TRACE_SCOPE("%s: commit", *this);
      if (this->_multi())
      {
        ELLE_DEBUG_SCOPE("%s: push blocks", *this);
        std::unordered_map<int, CacheEntry> blocks;
        std::swap(blocks, this->_blocks);
        for (auto& b: blocks)
        {
          // FIXME: incremental size compute
          ELLE_DEBUG("Checking data block %s :%x, size %s",
            b.first, b.second.block.address(), b.second.block.data().size());
          if (b.second.dirty)
          {
            ELLE_DEBUG("Writing data block %s", b.first);
            b.second.dirty = false;
            Address prev = b.second.block.address();
            Address addr = b.second.block.store(
              *this->_owner.block_store(),
              b.second.new_block ? model::STORE_INSERT : model::STORE_ANY);
            if (addr != prev)
            {
              ELLE_DEBUG("Changing address of block %s: %s -> %s", b.first,
                         prev, addr);
              int offset = (b.first + 1) * sizeof(Address);
              _first_block->data(
                [&] (elle::Buffer& data)
                {
                  if (data.size() < offset +sizeof(Address::Value))
                    data.size(offset + sizeof(Address::Value));
                  memcpy(data.contents() + offset, addr.value(),
                         sizeof(Address::Value));
                });
              if (!b.second.new_block)
                this->_owner.unchecked_remove(prev);
            }
            b.second.new_block = false;
            b.second.dirty = false;
          }
        }
      }
      else {
        Header h = _header();
        h.total_size = this->_first_block->data().size() - header_size;
        _header(h);
      }
      ELLE_DEBUG("store first block: %f with payload %s, total_size %s", *this->_first_block,
        this->_first_block->data().size() - header_size, _header().total_size)
      {
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
      }
      this->_first_block_new = false;
      ELLE_DEBUG("update parent directory");
      {
        if (_parent)
        {
          auto& data = _parent->_files.at(_name);
          data.mtime = time(nullptr);
          data.ctime = time(nullptr);
          if (!this->_multi())
            data.size = this->_first_block->data().size() - header_size;
          else
            data.size = _header().total_size;
        this->_parent->_commit({OperationType::update, _name}, false);
        }
      }
    }

    void
    File::_switch_to_multi(bool alloc_first_block)
    {
      // Switch without changing our address
      if (!_first_block)
        _first_block = elle::cast<MutableBlock>::runtime
          (_owner.fetch_or_die(_parent->_files.at(_name).address));
      uint64_t current_size = _first_block->data().size() - header_size;
      auto const& data = _first_block->data();
      auto new_block = _owner.block_store()->make_block<ImmutableBlock>(
        elle::Buffer(data.contents() + header_size, data.size()-header_size));
      ELLE_ASSERT_EQ(current_size, new_block->data().size());
      _blocks.insert(std::make_pair(0, CacheEntry{
        AnyBlock(std::move(new_block)), true, {}, true}));
      ELLE_ASSERT_EQ(current_size, _blocks.at(0).block.data().size());
      ELLE_ASSERT(_blocks.at(0).new_block);
      /*
      // current first_block becomes block[0], first_block becomes the index
      _blocks[0] = std::move(_first_block);
      _first_block = _owner.block_store()->make_block<Block>();
      _parent->_files.at(_name).address = _first_block->address();
      _parent->_commit();
      */
      _first_block->data
        ([] (elle::Buffer& data) { data.size(sizeof(Address)* 2); });
      // store block size in headers
      Header h {false, Header::current_version, default_block_size, 1, current_size};
      _header(h);
      ELLE_DEBUG("%s _switch_to_multi: storing b0 address %x", *this,
        _blocks.at(0).block.address());
      _first_block->data([&](elle::Buffer& data)
        {
          memcpy(data.mutable_contents() + sizeof(Address),
            _blocks.at(0).block.address().value(), sizeof(Address::Value));
        });

      // we know the operation that triggered us is going to expand data
      // beyond first block, so it is safe to resize here
      if (alloc_first_block)
      {
        auto& b = _blocks.at(0);
        int64_t old_size = b.block.data().size();
        b.block.data([] (elle::Buffer& data) {data.size(header_size + default_block_size);});
        if (old_size != signed(default_block_size + header_size))
          b.block.zero(old_size, default_block_size + header_size - old_size);
      }
      if (!_handle_count)
        this->_commit();
    }

    void
    File::check_cache()
    {
      typedef std::pair<const int, CacheEntry> Elem;
      bool fat_change = false;
      while (_blocks.size() > max_cache_size)
      {
        auto it = std::min_element(_blocks.begin(), _blocks.end(),
          [](Elem const& a, Elem const& b) -> bool
          {
            return a.second.last_use < b.second.last_use;
          });
        ELLE_DEBUG("Removing block %s from cache", it->first);
        if (it->second.dirty)
        {
          Address prev = it->second.block.address();
          Address addr = it->second.block.store(*_owner.block_store(),
                                                it->second.new_block? model::STORE_INSERT : model::STORE_ANY);
          if (addr != prev)
          {
            ELLE_DEBUG("Changing address of block %s: %s -> %s", it->first,
              prev, addr);
            fat_change = true;
            int offset = (it->first+1) * sizeof(Address);
            _first_block->data([&] (elle::Buffer& data)
              {
                if (data.size() < offset + sizeof(Address::Value))
                  data.size(offset +  sizeof(Address::Value));
                memcpy(data.contents() + offset, addr.value(), sizeof(Address::Value));
              });
            if (!it->second.new_block)
            {
              _owner.unchecked_remove(prev);
            }
          }
          it->second.new_block = false;
        }
        _blocks.erase(it);
      }
      if (fat_change)
      {
        _owner.store_or_die(std::move(_first_block),
                            _first_block_new ? model::STORE_INSERT : model::STORE_ANY);
        _first_block_new = false;
      }
    }

    std::vector<std::string> File::listxattr()
    {
      ELLE_TRACE("file listxattr");
      std::vector<std::string> res;
      /*
      res.push_back("user.infinit.block");
      res.push_back("user.infinit.auth.setr");
      res.push_back("user.infinit.auth.setrw");
      res.push_back("user.infinit.auth.setw");
      res.push_back("user.infinit.auth.clear");
      res.push_back("user.infinit.auth");
      */
      for (auto const& a: _parent->_files.at(_name).xattrs)
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
          return elle::sprintf("%x", elem.address);
        }
      }
      else if (key == "user.infinit.auth")
      {
        Address addr = _parent->_files.at(_name).address;
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
                        _parent->_files.at(_name).address);
      }
      else
        Node::setxattr(name, value, flags);
    }

    void
    File::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "File(\"%s\")", this->_name);
    }
  }
}
