#include <infinit/filesystem/filesystem.hh>

#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/SerializerOut.hh>

#include <reactor/filesystem.hh>
#include <reactor/scheduler.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/blocks/Block.hh>

ELLE_LOG_COMPONENT("infinit.fs");

namespace rfs = reactor::filesystem;

namespace infinit
{
  namespace filesystem
  {
    typedef infinit::model::blocks::Block Block;
    typedef infinit::model::Address Address;

    class Directory;
    class File;
    enum class FileStoreMode
    {
      direct = 0, // address points to file data
      index = 1   // address points to index of addresses
    };
    static Address::Value zeros = {0};
    struct FileData
    {
      std::string name;
      uint64_t size;
      uint32_t mode;
      uint64_t atime;
      uint64_t mtime;
      Address address;
      FileStoreMode store_mode;
      FileData(std::string name, uint64_t size, uint32_t mode, uint64_t atime,
        uint64_t mtime, Address const& address, FileStoreMode store_mode)
        : name(name)
        , size(size)
        , mode(mode)
        , atime(atime)
        , mtime(mtime)
        , address(address)
        , store_mode(store_mode)
      {
      }
      FileData()
        : address(zeros)
      {
      }
      FileData(elle::serialization::SerializerIn& s)
        : address(zeros)
      {
        serialize(s);
      }
      void serialize(elle::serialization::Serializer& s)
      {
        s.serialize("name", name);
        s.serialize("size", size);
        s.serialize("mode", mode);
        s.serialize("atime", atime);
        s.serialize("mtime", mtime);
        s.serialize("address", address);
        int sm = (int)store_mode;
        s.serialize("store_mode", sm);
        store_mode = (FileStoreMode)sm;
      }
    };
    struct CacheStats
    {
      int directories;
      int files;
      int blocks;
      long size;
    };
    #define THROW_NOENT { throw rfs::Error(ENOENT, "No such file or directory");}
    #define THROW_NOSYS { throw rfs::Error(ENOSYS, "Not implemented");}
    #define THROW_EXIST { throw rfs::Error(EEXIST, "File exists");}
    #define THROW_NOSYS { throw rfs::Error(ENOSYS, "Not implemented");}
    #define THROW_ISDIR { throw rfs::Error(EISDIR, "Is a directory");}
    #define THROW_NOTDIR { throw rfs::Error(ENOTDIR, "Is not a directory");}

    class Node
    {
    protected:
      Node(FileSystem& owner, Directory* parent, std::string const& name)
      : _owner(owner)
      , _parent(parent)
      , _name(name)
      {}
      void rename(boost::filesystem::path const& where);
      void utimens(const struct timespec tv[2]);
      void stat(struct stat* st);
      void _remove_from_cache();
      boost::filesystem::path full_path();
      FileSystem& _owner;
      Directory* _parent;
      std::string _name;
    };
    class Unknown: public Node, public rfs::Path
    {
    public:
      Unknown(Directory* parent, FileSystem& owner, std::string const& name);
      void stat(struct stat*) override
      {
        ELLE_DEBUG("Stat on unknown %s", _name);
        THROW_NOENT;
      }
      void list_directory(rfs::OnDirectoryEntry cb) override THROW_NOENT;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_NOENT;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
      void unlink() override THROW_NOENT;
      void mkdir(mode_t mode) override;
      void rmdir() override THROW_NOENT;
      void rename(boost::filesystem::path const& where) override THROW_NOENT;
      boost::filesystem::path readlink() override THROW_NOENT;
      void symlink(boost::filesystem::path const& where) override;
      void link(boost::filesystem::path const& where) override;
      void chmod(mode_t mode) override THROW_NOENT;
      void chown(int uid, int gid) override THROW_NOENT;
      void statfs(struct statvfs *) override THROW_NOENT;
      void utimens(const struct timespec tv[2]) override THROW_NOENT;
      void truncate(off_t new_size) override THROW_NOENT;
      std::unique_ptr<Path> child(std::string const& name) override THROW_NOENT;
    private:
    };

    class Directory: public rfs::Path, public Node
    {
    public:
      Directory(Directory* parent, FileSystem& owner, std::string const& name,
                std::unique_ptr<Block> b);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) override;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_ISDIR;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override THROW_ISDIR;
      void unlink() override THROW_ISDIR;
      void mkdir(mode_t mode) override THROW_EXIST;
      void rmdir() override;
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override  THROW_ISDIR;
      void symlink(boost::filesystem::path const& where) override THROW_EXIST;
      void link(boost::filesystem::path const& where) override THROW_EXIST;
      void chmod(mode_t mode) override THROW_NOSYS;
      void chown(int uid, int gid) override THROW_NOSYS;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override THROW_ISDIR;
      std::unique_ptr<rfs::Path> child(std::string const& name) override;

      void cache_stats(CacheStats& append);
    private:
      void move_recurse(boost::filesystem::path const& current,
                        boost::filesystem::path const& where);
      friend class Unknown;
      friend class File;
      friend class Node;
      friend class FileHandle;
      void _changed(bool set_mtime = false);
      void _push_changes();
      std::unique_ptr<Block> _block;
      std::unordered_map<std::string, FileData> _files;
      friend class FileSystem;
    };

    class FileHandle: public rfs::Handle
    {
    public:
      FileHandle(File& owner);
      ~FileHandle();
      int read(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      int write(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      void ftruncate(off_t offset) override;
      void close() override;
    private:
      File& _owner;
      bool _dirty;
    };

    class File: public rfs::Path, public Node
    {
    public:
      File(Directory* parent, FileSystem& owner, std::string const& name,
                std::unique_ptr<Block> b);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) THROW_NOTDIR;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override;
      void unlink() override;
      void mkdir(mode_t mode) override THROW_EXIST;
      void rmdir() override THROW_NOTDIR;
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override  THROW_NOENT;
      void symlink(boost::filesystem::path const& where) override THROW_EXIST;
      void link(boost::filesystem::path const& where) override THROW_EXIST;
      void chmod(mode_t mode) override THROW_NOSYS;
      void chown(int uid, int gid) override THROW_NOSYS;
      void statfs(struct statvfs *) override THROW_NOSYS;
      void utimens(const struct timespec tv[2]);
      void truncate(off_t new_size) override;
      std::unique_ptr<Path> child(std::string const& name) override THROW_NOTDIR;
    private:
      static const unsigned long default_block_size = 1024 * 1024;
      friend class FileHandle;
      friend class Directory;
      /* Get address for given block index.
       * @param create: if true, allow creation of a new block as needed
       * @param change: will be filled by true if creation was/would have
       *                been required
      */
      boost::optional<Address>
      _block_address(int index, bool create, bool* change = nullptr);
      void _switch_to_multi();
      void _changed();
      long _block_size();
      bool _multi(); // True if mode is index
      std::unordered_map<int, std::unique_ptr<Block>> _blocks;
      std::unique_ptr<Block> _first_block;
    };


    static Address to_address(std::string const& str)
    {
      Address::Value v;
      char c[3] = {0,0,0};
      if (str.length() != 64)
        throw std::runtime_error("Invalid address size");
      for (int i=0; i<32; ++i)
      {
        c[0] = str[i*2];
        c[1] = str[i*2 + 1];
        v[i] = strtol(c, nullptr, 16);
      }
      return Address(v);
    }

    FileSystem::FileSystem(std::string root,
                           std::unique_ptr<infinit::model::Model> block_store)
    : _block_store(std::move(block_store))
    , _root_address(root)
    {
      reactor::scheduler().signal_handle(SIGUSR1, [this] { this->print_cache_stats();});
    }
    void
    FileSystem::print_cache_stats()
    {
      Directory& root = dynamic_cast<Directory&>(fs()->path("/"));
      CacheStats stats;
      memset(&stats, 0, sizeof(CacheStats));
      root.cache_stats(stats);
      std::cerr << "Statistics:\n"
      << stats.directories << " dirs\n"
      << stats.files << " files\n"
      << stats.blocks <<" blocks\n"
      << stats.size << " bytes"
      << std::endl;
    }

    std::unique_ptr<rfs::Path>
    FileSystem::path(std::string const& path)
    {
      std::unique_ptr<Block> block;
      if (_root_address.empty())
      {
        block = block_store()->make_block();
        ELLE_LOG("New root address: %x", block->address());
      }
      else
        block = block_store()->fetch(to_address(_root_address));
      Directory* res = new Directory(nullptr, *this, "", std::move(block));
      res->_changed();
      return std::unique_ptr<rfs::Path>(res);
    }

    static const int DIRECTORY_MASK = 0040000;

    Directory::Directory(Directory* parent, FileSystem& owner,
                         std::string const& name,
                         std::unique_ptr<Block> b)
    : Node(owner, parent, name)
    , _block(std::move(b))
    {
      ELLE_DEBUG("Directory::Directory %s, parent %s", this, parent);
      if (!_block->data().empty())
      {
        ELLE_DEBUG("Deserializing directory");
        std::istream is(new elle::InputStreamBuffer<elle::Buffer>(_block->data()));
        elle::serialization::json::SerializerIn input(is);
        input.serialize("content", _files);
      }
      else
        _changed();
    }
    void
    Directory::statfs(struct statvfs * st)
    {
      memset(st, 0, sizeof(struct statvfs));
      st->f_bsize = 32768;
      st->f_frsize = 32768;
      st->f_blocks = 1000000;
      st->f_bavail = 1000000;
      st->f_fsid = 1;
    }
    void
    Directory::_changed(bool set_mtime)
    {
      ELLE_DEBUG("Directory changed: %s with %s entries", this, _files.size());
      {
        _block->data().reset();
        std::ostream os(new elle::OutputStreamBuffer<elle::Buffer>(_block->data()));
        elle::serialization::json::SerializerOut output(os);
        output.serialize("content", _files);
      }
      if (set_mtime && _parent)
      {
        _parent->_files.at(_name).mtime = time(nullptr);
        _parent->_changed();
      }
      _push_changes();
    }
    void
    Directory::_push_changes()
    {
      ELLE_DEBUG("Directory pushChanges: %s on %x size %s",
                 this, _block->address(), _block->data().size());
      _owner.block_store()->store(*_block);
      ELLE_DEBUG("pushChange ok");
    }
    std::unique_ptr<rfs::Path>
    Directory::child(std::string const& name)
    {
      ELLE_DEBUG_SCOPE("Directory child: %s / %s", *this, name);
      auto it = _files.find(name);
      if (it != _files.end())
      {
        bool isdir = it->second.mode & DIRECTORY_MASK;
        if (isdir)
          return std::unique_ptr<rfs::Path>(new Directory(this, _owner, name,
                               _owner.block_store()->fetch(it->second.address)));
        else
          return std::unique_ptr<rfs::Path>(new File(this, _owner, name,
                          _owner.block_store()->fetch(it->second.address)));
      }
      else
        return std::unique_ptr<rfs::Path>(new Unknown(this, _owner, name));
    }

    void
    Directory::list_directory(rfs::OnDirectoryEntry cb)
    {
      ELLE_DEBUG("Directory list: %s", this);
      struct stat st;
      for (auto const& e: _files)
      {
        st.st_mode = e.second.mode;
        st.st_size = e.second.size;
        cb(e.first, &st);
      }
    }

    void Directory::rmdir()
    {
      if (!_files.empty())
        throw rfs::Error(ENOTEMPTY, "Directory not empty");
      if (&_parent == nullptr)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      _parent->_files.erase(_name);
      _parent->_changed();
      _owner.block_store()->remove(_block->address());
      _remove_from_cache();
    }

    void Directory::move_recurse(boost::filesystem::path const& current,
                                 boost::filesystem::path const& where)
    {
      for (auto const& v: _files)
      {
        std::string const& name = v.first;
        ELLE_DEBUG("Extracting %s", current / name);
        auto p = _owner.fs()->extract((current / name).string());
        if (p)
        {
          auto ptr = p.get();
          ELLE_DEBUG("Inserting %s", where / name);
          _owner.fs()->set((where/name).string(), std::move(p));
          if (v.second.mode & DIRECTORY_MASK)
          {
            dynamic_cast<Directory*>(ptr)->move_recurse(current / name, where / name);
          }
        }
      }
    }
    void Directory::rename(boost::filesystem::path const& where)
    {
      boost::filesystem::path current = full_path();
      Node::rename(where);
      // We might have children that pointed to us, we need to move them
      this->move_recurse(current, where);
    }

    void Node::rename(boost::filesystem::path const& where)
    {
      boost::filesystem::path current = full_path();
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();

      if (&_parent == nullptr)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      Directory& dir = dynamic_cast<Directory&>(
        _owner.fs()->path(newpath.string()));

      if (dir._files.find(newname) != dir._files.end())
      {
        // File and empty dir gets removed.
        rfs::Path& target = _owner.fs()->path(where.string());
        struct stat st;
        target.stat(&st);
        if (st.st_mode & DIRECTORY_MASK)
        {
          try
          {
            target.rmdir();
          }
          catch(rfs::Error const& e)
          {
            throw rfs::Error(EISDIR, "Target is a directory");
          }
        }
        else
          target.unlink();
        ELLE_DEBUG("removed move target %s", where);
      }
      auto data = _parent->_files.at(_name);
      _parent->_files.erase(_name);
      _parent->_changed();

      data.name = newname;
      dir._files.insert(std::make_pair(newname, data));
      dir._changed();
      _name = newname;
      _parent = &dir;
      // Move the node in cache
      ELLE_DEBUG("Extracting %s", current);
      auto p = _owner.fs()->extract(current.string());
      // This might delete the dummy Unknown on destination which is fine
       ELLE_DEBUG("Setting %s", where);
      _owner.fs()->set(where.string(),std::move(p));
    }

    void Node::_remove_from_cache()
    {
      ELLE_DEBUG("remove_from_cache: %s entering", _name);
      std::unique_ptr<rfs::Path> self = _owner.fs()->extract(full_path().string());
      rfs::Path* p = self.release();
      ELLE_DEBUG("remove_from_cache: %s released", _name);
      new reactor::Thread("delayed_cleanup", [p] { ELLE_DEBUG("async_clean"); delete p;}, true);
      ELLE_DEBUG("remove_from_cache: %s exiting with async cleaner", _name);
    }

    boost::filesystem::path
    Node::full_path()
    {
      if (_parent == nullptr)
        return "/";
      return _parent->full_path() / _name;
    }

    void Directory::stat(struct stat* st)
    {
      ELLE_DEBUG("stat on dir %s", _name);
      Node::stat(st);
    }

    void
    Directory::cache_stats(CacheStats& cs)
    {
      cs.directories++;
      boost::filesystem::path current = full_path();
      for(auto const& f: _files)
      {
        rfs::Path* p = _owner.fs()->get((current / f.second.name).string());
        if (!p)
          return;
        if (Directory* d = dynamic_cast<Directory*>(p))
          d->cache_stats(cs);
        else if (File* f = dynamic_cast<File*>(p))
        {
          cs.files++;
          cs.blocks += 1 + f->_blocks.size();
          for (auto const& b: f->_blocks)
            cs.size += b.second->data().size();
        }
      }
    }

    void
    Node::stat(struct stat* st)
    {

      memset(st, 0, sizeof(struct stat));
      if (_parent)
      {
        auto fd = _parent->_files.at(_name);
        st->st_blksize = 16384;
        st->st_mode = fd.mode;
        st->st_size = fd.size;
        st->st_atime = fd.atime;
        st->st_mtime = fd.mtime;
        st->st_ctime = fd.mtime;
        st->st_dev = 1;
        st->st_ino = (long)this;
        st->st_nlink = 1;
      }
      else
      {
        st->st_mode = DIRECTORY_MASK;
        st->st_size = 0;
      }
    }

    Unknown::Unknown(Directory* parent, FileSystem& owner, std::string const& name)
    : Node(owner, parent, name)
    {}

    void Unknown::mkdir(mode_t mode)
    {
      ELLE_DEBUG("mkdir %s", _name);
      std::unique_ptr<Block> b = _owner.block_store()->make_block();
      _owner.block_store()->store(*b);
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      _parent->_files.insert(
        std::make_pair(_name,
                       FileData{_name, 0, mode | DIRECTORY_MASK,
                                uint64_t(time(nullptr)),
                                uint64_t(time(nullptr)),
                                b->address(),
                                FileStoreMode::direct}));
      _parent->_changed();
      _remove_from_cache();
    }

    std::unique_ptr<rfs::Handle>
    Unknown::create(int flags, mode_t mode)
    {
      std::unique_ptr<Block> b = _owner.block_store()->make_block();
      _owner.block_store()->store(*b);
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      _parent->_files.insert(
        std::make_pair(_name, FileData{_name, 0, mode & ~DIRECTORY_MASK,
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       b->address(),
                                       FileStoreMode::direct}));
      _parent->_changed(true);
      _remove_from_cache();
      File& f = dynamic_cast<File&>(_owner.fs()->path(full_path().string()));
      return std::unique_ptr<rfs::Handle>(new FileHandle(f));
    }

    void
    Unknown::symlink(boost::filesystem::path const& where)
    {
      throw rfs::Error(ENOSYS, "Not implemented");
    }

    void
    Unknown::link(boost::filesystem::path const& where)
    {
      throw rfs::Error(ENOSYS, "Not implemented");
    }

    File::File(Directory* parent, FileSystem& owner, std::string const& name,
               std::unique_ptr<Block> block)
    : Node(owner, parent, name)
    , _first_block(std::move(block))
    {
    }

    bool
    File::_multi()
    {
      return _parent->_files.at(_name).store_mode == FileStoreMode::index;
    }

    long
    File::_block_size()
    {
      if (!_multi() || _first_block->data().size() < 4)
        return default_block_size;
      uint32_t nbs;
      memcpy(&nbs, _first_block->data().mutable_contents(), 4);
      nbs = ntohl(nbs);
      return nbs;
    }

    boost::optional<Address>
    File::_block_address(int index, bool create, bool* changeOrAbort)
    {
      if (changeOrAbort)
        *changeOrAbort = false;
      int offset = (index+1) * sizeof(Address);
      int sz = _first_block->data().size();
      if (_first_block->data().size() < offset + sizeof(Address))
      {
        if (!create)
        {
          if (changeOrAbort)
            *changeOrAbort = true;
          return boost::optional<Address>();
        }
        _first_block->data().size(offset + sizeof(Address));
        memset(_first_block->data().mutable_contents() + sz, 0,
               offset + sizeof(Address) - sz);
      }
      char zeros[sizeof(Address)];
      memset(zeros, 0, sizeof(Address));
      if (!memcmp(zeros, _first_block->data().mutable_contents() + offset,
                 sizeof(Address)))
      { // allocate
        if (!create)
        {
          if (changeOrAbort)
            *changeOrAbort = true;
          return boost::optional<Address>();
        }
        std::unique_ptr<Block> b = _owner.block_store()->make_block();
        _owner.block_store()->store(*b);
        memcpy(_first_block->data().mutable_contents() + offset,
               b->address().value(), sizeof(Address::Value));
        if (changeOrAbort)
          *changeOrAbort = true;
        return b->address();
      }
      return Address(*(Address*)(_first_block->data().mutable_contents() + offset));
    }


    void
    File::unlink()
    {
      _parent->_files.erase(_name);
      _parent->_changed(true);
      if (!_multi())
        _owner.block_store()->remove(_first_block->address());
      else
      {
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
      _remove_from_cache();
    }

    void
    File::rename(boost::filesystem::path const& where)
    {
      Node::rename(where);
    }

    void
    File::stat(struct stat* st)
    {
      ELLE_DEBUG("stat on file %s", _name);
      Node::stat(st);
      st->st_size = _parent->_files.at(_name).size;
    }

    void
    File::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    void
    Directory::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    void
    Node::utimens(const struct timespec tv[2])
    {
      auto & f = _parent->_files.at(_name);
      f.atime = tv[0].tv_sec;
      f.mtime = tv[1].tv_sec;
      _parent->_changed();
    }

    void
    File::truncate(off_t new_size)
    {
      if (!_multi() && new_size > _block_size())
        _switch_to_multi();
      if (!_multi())
      {
        _first_block->data().size(new_size);
      }
      else
      { // FIXME: addr should be a Address::Value
        Address::Value zero;
        memset(&zero, 0, sizeof(Address::Value));
        Address* addr = (Address*)(void*)_first_block->data().mutable_contents();
        addr++; // skip header
        off_t drop_from = new_size? (new_size-1) / _block_size() : 0;
        for (unsigned i=drop_from + 1; i*sizeof(Address) <= _first_block->data().size(); ++i)
        {
           if (!memcmp(addr[i].value(), zero, sizeof(Address::Value)))
            continue; // unallocated block
          _owner.block_store()->remove(addr[i]);
          _blocks.erase(i);
        }
        _first_block->data().size((drop_from+1)*sizeof(Address));
        // last block surviving the cut might need resizing
        if (!memcmp(addr[drop_from].value(), zero, sizeof(Address::Value)))
        {
          Block* bl;
          auto it = _blocks.find(drop_from);
          if (it != _blocks.end())
            bl = it->second.get();
          else
          {
            _blocks[drop_from] = _owner.block_store()->fetch(addr[drop_from]);
            bl = _blocks[drop_from].get();
          }
          bl->data().size(new_size % _block_size());
        }
        if (new_size <= _block_size())
        {
          auto& data = _parent->_files.at(_name);
          data.store_mode = FileStoreMode::direct;
          data.address = addr[0];
          data.size = new_size;
          _parent->_changed();
          _owner.block_store()->remove(_first_block->address());
          auto it = _blocks.find(0);
          if (it != _blocks.end())
            _first_block = std::move(it->second);
          else
             _first_block = _owner.block_store()->fetch(addr[0]);
          _blocks.clear();
        }
      }
      _changed();
    }

    std::unique_ptr<rfs::Handle>
    File::open(int flags, mode_t mode)
    {
      if (flags & O_TRUNC)
        truncate(0);
      return std::unique_ptr<rfs::Handle>(new FileHandle(*this));
    }

    std::unique_ptr<rfs::Handle>
    File::create(int flags, mode_t mode)
    {
      if (flags & O_TRUNC)
        truncate(0);
      return std::unique_ptr<rfs::Handle>(new FileHandle(*this));
    }

    void
    File::_changed()
    {
      _owner.block_store()->store(*_first_block);
      auto& data = _parent->_files.at(_name);
      data.mtime = time(nullptr);
      if (!_multi())
        data.size = _first_block->data().size();
      else
      {
        int64_t sz = 0;
        for (auto const& b: _blocks)
        {
          sz += b.second->data().size();
          _owner.block_store()->store(*b.second);
        }
        data.size = sz;
      }
      _parent->_changed(false);
    }

    void
    File::_switch_to_multi()
    {
      // current first_block becomes block[0], first_block becomes the index
      _parent->_files.at(_name).store_mode = FileStoreMode::index;
      _blocks[0] = std::move(_first_block);
      _first_block = _owner.block_store()->make_block();
      _parent->_files.at(_name).address = _first_block->address();
      _parent->_changed();
      _first_block->data().size(sizeof(Address)* 2);
      // store block size in headers
      uint32_t bs = default_block_size;
      bs = htonl(bs);
      memcpy(_first_block->data().mutable_contents(), &bs, 4);
      memcpy(_first_block->data().mutable_contents() + sizeof(Address),
        _blocks.at(0)->address().value(), sizeof(Address::Value));
      // we know the operation that triggered us is going to expand data
      // beyond first block, so it is safe to resize here
      _blocks.at(0)->data().size(default_block_size);
    }

    FileHandle::FileHandle(File& owner)
    : _owner(owner)
    , _dirty(false)
    {
      _owner._parent->_files.at(_owner._name).atime = time(nullptr);
      _owner._parent->_changed(false);
    }

    FileHandle::~FileHandle()
    {
      close();
    }

    void
    FileHandle::close()
    {
      if (_dirty)
        _owner._changed();
    }

    int
    FileHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      int64_t total_size = _owner._parent->_files.at(_owner._name).size;
      int32_t block_size = _owner._block_size();
      if (offset >= total_size)
        return 0;
      if (signed(offset + size) >= total_size)
        size = total_size - offset;
      if (!_owner._multi())
      { // single block case
        auto& block = _owner._first_block;
        ELLE_ASSERT(block->data().size() == total_size);
        memcpy(buffer.mutable_contents(), &block->data()[offset], size);
        return size;
      }

      // multi case
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      { // single block case
        off_t block_offset = offset - start_block * block_size;
        auto const& it = _owner._blocks.find(start_block);
        Block* block = nullptr;
        if (it != _owner._blocks.end())
        {
          block = it->second.get();
        }
        else
        {
          bool change;
          boost::optional<Address> addr = _owner._block_address(start_block, false, &change);
          if (change)
          { // block would have been allocated: sparse file?
            memset(buffer.mutable_contents(), 0, size);
            return size;
          }
          _owner._blocks.insert(std::make_pair(start_block, _owner._owner.block_store()->fetch(*addr)));
          block = _owner._blocks.find(start_block)->second.get();
        }
        ELLE_ASSERT(block->data().size() >= block_offset + size);
        memcpy(buffer.mutable_contents(), &block->data()[block_offset], size);
        return size;
      }
      else
      { // overlaps two blocks case
        ELLE_ASSERT(start_block == end_block - 1);
        int64_t second_size = (offset + size) % block_size; // second block
        int64_t first_size = size - second_size;
        int64_t second_offset = end_block * block_size;
        int r1 = read(elle::WeakBuffer(buffer.mutable_contents(), first_size),
                      first_size, offset);
        if (r1 <= 0)
          return r1;
        int r2 = read(elle::WeakBuffer(buffer.mutable_contents() + first_size, second_size),
                 second_size, second_offset);
        if (r2 < 0)
          return r2;
        return r1 + r2;
      }
    }

    int
    FileHandle::write(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      _dirty = true;
      ELLE_DEBUG("write %s at %s on %s", size, offset, _owner._name);
      uint32_t block_size = _owner._block_size();
      if (!_owner._multi() && size + offset > block_size)
        _owner._switch_to_multi();
      if (!_owner._multi())
      {
        auto& block = _owner._first_block;
        if (offset + size > block->data().size())
          block->data().size(offset + size);
        memcpy(&block->data()[offset], buffer.mutable_contents(), size);
        return size;
      }
      // multi mode
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      {
        Block* block;
        auto const it = _owner._blocks.find(start_block);
        if (it != _owner._blocks.end())
          block = it->second.get();
        else
        {
          bool change;
          boost::optional<Address> addr = _owner._block_address(start_block, true, &change);
          _owner._blocks.insert(std::make_pair(start_block, _owner._owner.block_store()->fetch(*addr)));
          block = _owner._blocks[start_block].get();
        }
        off_t block_offset = offset % block_size;
        if (block->data().size() < block_offset + size)
          block->data().size(block_offset + size);
        memcpy(&block->data()[block_offset], buffer.mutable_contents(), size);
        return size;
      }
      // write across blocks
      ELLE_ASSERT(start_block == end_block - 1);
      int64_t second_size = (offset + size) % block_size; // second block
      int64_t first_size = size - second_size;
      int64_t second_offset = end_block * block_size;
      int r1 = write(elle::WeakBuffer(buffer.mutable_contents(), first_size),
                    first_size, offset);
      if (r1 <= 0)
        return r1;
      int r2 = write(elle::WeakBuffer(buffer.mutable_contents() + first_size, second_size),
                    second_size, second_offset);
      if (r2 < 0)
        return r2;
      return r1 + r2;
    }

    void
    FileHandle::ftruncate(off_t offset)
    {
      return _owner.truncate(offset);
    }
  }
}
