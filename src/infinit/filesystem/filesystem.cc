#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/MissingBlock.hh>

#include <elle/log.hh>
#include <elle/os/environ.hh>

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
      void link(boost::filesystem::path const& where) override;
      void chmod(mode_t mode) override THROW_NOSYS;
      void chown(int uid, int gid) override THROW_NOSYS;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]);
      void truncate(off_t new_size) override;
      std::unique_ptr<Path> child(std::string const& name) override THROW_NOTDIR;
      // check cache size, remove entries if needed
      void check_cache();
    private:
      static const unsigned long default_block_size = 1024 * 1024;
      static const unsigned long max_cache_size = 20; // in blocks
      friend class FileHandle;
      friend class Directory;
      // A packed network-byte-ordered version of Header sits at the
      // beginning of each file's first block in index mode.
      struct Header
      { // max size we can grow is sizeof(Address)
        uint32_t block_size;
        uint32_t links;
        uint64_t total_size;
      };
      /* Get address for given block index.
       * @param create: if true, allow creation of a new block as needed
       * @param change: will be filled by true if creation was/would have
       *                been required
      */
      boost::optional<Address>
      _block_address(int index, bool create, bool* change = nullptr);
      // Switch from direct to indexed mode
      void _switch_to_multi(bool alloc_first_block);
      void _changed();
      Header _header(); // Get header, must be in multi mode
      void _header(Header const&);
      bool _multi(); // True if mode is index
      struct CacheEntry
      {
        std::unique_ptr<Block> block;
        bool dirty;
        std::chrono::system_clock::time_point last_use;
      };
      std::unordered_map<int, CacheEntry> _blocks;
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
        std::unique_ptr<Block> block;
        try
        {
          block = _owner.block_store()->fetch(it->second.address);
        }
        catch (infinit::model::MissingBlock const& b)
        {
          throw rfs::Error(EIO, b.what());
        }
        bool isdir = it->second.mode & DIRECTORY_MASK;
        if (isdir)
          return std::unique_ptr<rfs::Path>(new Directory(this, _owner, name,
                                                          std::move(block)));
        else
          return std::unique_ptr<rfs::Path>(new File(this, _owner, name,
                                                     std::move(block)));
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
          if (f->_first_block)
            cs.size += f->_first_block->data().size();
          for (auto const& b: f->_blocks)
            cs.size += b.second.block->data().size();
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
      { // Root directory permissions
        st->st_mode = DIRECTORY_MASK | 0777;
        st->st_size = 0;
      }
      st->st_uid = ::getuid();
      st->st_gid = ::getgid();
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
      throw rfs::Error(ENOENT, "link source does not exist");
    }

    File::File(Directory* parent, FileSystem& owner, std::string const& name,
               std::unique_ptr<Block> block)
    : Node(owner, parent, name)
    , _first_block(std::move(block))
    {
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
      return _parent->_files.at(_name).store_mode == FileStoreMode::index;
    }

    File::Header
    File::_header()
    {
      Header res;
      uint32_t v;
      memcpy(&v, _first_block->data().mutable_contents(), 4);
      res.block_size = ntohl(v);
      memcpy(&v, _first_block->data().mutable_contents()+4, 4);
      res.links = ntohl(v);
      uint64_t v2;
      memcpy(&v2, _first_block->data().mutable_contents()+8, 8);
      res.total_size = ((uint64_t)ntohl(v2)<<32) + ntohl(v2 >> 32);
      return res;
      ELLE_DEBUG("Header: bs=%s links=%s size=%s", res.block_size, res.links, res.total_size);
    }

    void
    File::_header(Header const& h)
    {
      uint32_t v = htonl(h.block_size);
      memcpy(_first_block->data().mutable_contents(), &v, 4);
      v = htonl(h.links);
      memcpy(_first_block->data().mutable_contents()+4, &v, 4);
      uint64_t v2 = (uint64_t)htonl(h.total_size)<<32 + htonl(h.total_size >> 32);
      memcpy(_first_block->data().mutable_contents()+8, &v2, 8);
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
    File::link(boost::filesystem::path const& where)
    {
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();
      Directory& dir = dynamic_cast<Directory&>(
        _owner.fs()->path(newpath.string()));
      if (dir._files.find(newname) != dir._files.end())
        throw rfs::Error(EEXIST, "target file exists");
      // we need a place to store the link count
      bool multi = _multi();
      if (!multi)
        _switch_to_multi(false);
      Header header = _header();
      header.links++;
      _header(header);
      dir._files.insert(std::make_pair(newname, _parent->_files.at(_name)));
      dir._files.at(newname).name = newname;
      dir._changed(true);
      _owner.fs()->extract(where.string());
      _owner.block_store()->store(*_first_block);
      // also flush new data block
      if (!multi)
        _owner.block_store()->store(*_blocks.at(0).block);
    }

    void
    File::unlink()
    {
      static bool no_unlink = !elle::os::getenv("INHIBIT_UNLINK", "").empty();
      if (no_unlink)
      { // DEBUG: link the file in root directory
        std::string n("__" + full_path().string());
        for (unsigned int i=0; i<n.length(); ++i)
          if (n[i] == '/')
          n[i] = '_';
        Directory* dir = _parent;
        while (dir->_parent)
          dir = dir->_parent;
        auto& cur = _parent->_files.at(_name);
        dir->_files.insert(std::make_pair(n, cur));
        dir->_files.at(n).name = n;
      }
      // links and multi methods can't be called after deletion from parent
      bool multi = _multi();
      int links = 1;
      if (multi)
        links = _header().links;
      _parent->_files.erase(_name);
      _parent->_changed(true);
      if (!multi)
      {
        if (!no_unlink)
          _owner.block_store()->remove(_first_block->address());
      }
      else
      {
        _first_block = _owner.block_store()->fetch(_first_block->address());
        if (links > 1)
        {
          ELLE_DEBUG("%s remaining links", links - 1);
          Header header = _header();
          header.links--;
          _header(header);
          _owner.block_store()->store(*_first_block);
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
      if (_multi())
      {
        _first_block = _owner.block_store()->fetch(_first_block->address());
        Header header = _header();
        st->st_size = header.total_size;
        ELLE_DEBUG("stat on multi: %s", header.total_size);
      }
      else
      {
        st->st_size = _parent->_files.at(_name).size;
        ELLE_DEBUG("stat on single: %s", st->st_size);
      }
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
      if (!_multi() && new_size > signed(default_block_size))
        _switch_to_multi(true);
      if (!_multi())
      {
        _first_block->data().size(new_size);
      }
      else
      { // FIXME: addr should be a Address::Value
        Header header = _header();
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
        _first_block->data().size((drop_from+2)*sizeof(Address));
        // last block surviving the cut might need resizing
        if (drop_from >=0 && !memcmp(addr[drop_from+1].value(), zero, sizeof(Address::Value)))
        {
          Block* bl;
          auto it = _blocks.find(drop_from);
          if (it != _blocks.end())
          {
            bl = it->second.block.get();
            it->second.dirty = true;
          }
          else
          {
            _blocks[drop_from].block = _owner.block_store()->fetch(addr[drop_from+1]);
            CacheEntry& ent = _blocks[drop_from];
            bl = ent.block.get();
            ent.dirty = true;
          }
          bl->data().size(new_size % block_size);
        }
        header.total_size = new_size;
        _header(header);
        if (new_size <= block_size && header.links == 1)
        {
          auto& data = _parent->_files.at(_name);
          data.store_mode = FileStoreMode::direct;
          data.address = addr[1];
          data.size = new_size;
          _parent->_changed();
          _owner.block_store()->remove(_first_block->address());
          auto it = _blocks.find(0);
          if (it != _blocks.end())
            _first_block = std::move(it->second.block);
          else
            _first_block = _owner.block_store()->fetch(addr[1]);
          _first_block->data().size(new_size);
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
      auto& data = _parent->_files.at(_name);
      data.mtime = time(nullptr);
      if (!_multi())
        data.size = _first_block->data().size();
      else
      {
        for (auto& b: _blocks)
        { // FIXME: incremental size compute
          ELLE_DEBUG("Checking data block %s :%x, size %s",
            b.first, b.second.block->address(), b.second.block->data().size());
          if (b.second.dirty)
          {
            b.second.dirty = false;
            _owner.block_store()->store(*b.second.block);
          }
        }
      }
      ELLE_DEBUG("Storing first block %x, size %s",
                 _first_block->address(), _first_block->data().size());
      _owner.block_store()->store(*_first_block);
      _parent->_changed(false);
    }

    void
    File::_switch_to_multi(bool alloc_first_block)
    {
      // Switch without changing our address
      _parent->_files.at(_name).store_mode = FileStoreMode::index;
      _parent->_changed();
      uint64_t current_size = _first_block->data().size();
      std::unique_ptr<Block> new_block = _owner.block_store()->make_block();
      new_block->data() = std::move(_first_block->data());
      _blocks.insert(std::make_pair(0, CacheEntry{std::move(new_block), true}));

      /*
      // current first_block becomes block[0], first_block becomes the index
      _blocks[0] = std::move(_first_block);
      _first_block = _owner.block_store()->make_block();
      _parent->_files.at(_name).address = _first_block->address();

      _parent->_changed();
      */
      _first_block->data().size(sizeof(Address)* 2);
      // store block size in headers
      Header h { default_block_size, 1, current_size};
      _header(h);

      memcpy(_first_block->data().mutable_contents() + sizeof(Address),
        _blocks.at(0).block->address().value(), sizeof(Address::Value));
      // we know the operation that triggered us is going to expand data
      // beyond first block, so it is safe to resize here
      if (alloc_first_block)
      {
        auto& b = _blocks.at(0);
        int64_t old_size = b.block->data().size();
        b.block->data().size(default_block_size);
        if (old_size != default_block_size)
          memset(b.block->data().mutable_contents() + old_size, 0, default_block_size - old_size);
      }
      _changed();
    }

    void
    File::check_cache()
    {
      typedef std::pair<const int, CacheEntry> Elem;
      while (_blocks.size() > max_cache_size)
      {
        auto it = std::min_element(_blocks.begin(), _blocks.end(),
          [](Elem const& a, Elem const& b) -> bool
          {
            return a.second.last_use < b.second.last_use;
          });
        ELLE_DEBUG("Removing block %s from cache", it->first);
        if (it->second.dirty)
          _owner.block_store()->store(*it->second.block);
        _blocks.erase(it);
      }
    }

    FileHandle::FileHandle(File& owner)
    : _owner(owner)
    , _dirty(false)
    {
      _owner._parent->_files.at(_owner._name).atime = time(nullptr);
      _owner._parent->_changed(false);
      // FIXME: the only thing that can invalidate _owner is hard links
      // keep tracks of open handle to know if we should refetch
      // or a backend stat call?

      _owner._first_block = _owner._owner.block_store()->fetch(
        _owner._first_block->address());
    }

    FileHandle::~FileHandle()
    {
      close();
    }

    void
    FileHandle::close()
    {
      ELLE_DEBUG("Closing with dirty=%s", _dirty);
      if (_dirty)
        _owner._changed();
      _dirty = false;
      _owner._blocks.clear();
    }

    int
    FileHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      ELLE_ASSERT_EQ(buffer.size(), size);
      int64_t total_size;
      int32_t block_size;
      if (_owner._multi())
      {
        File::Header h = _owner._header();
        total_size = h.total_size;
        block_size = h.block_size;
      }
      else
      {
        total_size = _owner._parent->_files.at(_owner._name).size;
        block_size = _owner.default_block_size;
      }

      if (offset >= total_size)
      {
        ELLE_DEBUG("read past end");
        return 0;
      }
      if (signed(offset + size) >= total_size)
        size = total_size - offset;
      if (!_owner._multi())
      { // single block case
        auto& block = _owner._first_block;
        ELLE_ASSERT_EQ(block->data().size(), total_size);
        memcpy(buffer.mutable_contents(),
               block->data().mutable_contents() + offset,
               size);
        ELLE_DEBUG("read %s bytes", size);
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
          block = it->second.block.get();
          it->second.last_use = std::chrono::system_clock::now();
        }
        else
        {
          bool change;
          boost::optional<Address> addr = _owner._block_address(start_block, false, &change);
          if (change)
          { // block would have been allocated: sparse file?
            memset(buffer.mutable_contents(), 0, size);
            ELLE_DEBUG("read %s 0-bytes", size);
            return size;
          }
          auto inserted = _owner._blocks.insert(std::make_pair(start_block,
            File::CacheEntry{_owner._owner.block_store()->fetch(*addr), false}));
          block = inserted.first->second.block.get();
          inserted.first->second.last_use = std::chrono::system_clock::now();
          _owner.check_cache();
        }
        ELLE_ASSERT_LTE(block_offset + size, block_size);

        if (block->data().size() < block_offset + size)
        { // sparse file, eof shrinkage of size was handled above
          long available = block->data().size() - block_offset;
          ELLE_DEBUG("no data for %s out of %s bytes", size - available, size);
          memcpy(buffer.mutable_contents(),
                 block->data().contents() + block_offset,
                 available);
          memset(buffer.mutable_contents() + available, 0, size - available);
        }
        memcpy(buffer.mutable_contents(), &block->data()[block_offset], size);
        ELLE_DEBUG("read %s bytes", size);
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
        ELLE_DEBUG("read %s+%s=%s bytes", r1, r2, r1+r2);
        return r1 + r2;
      }
    }

    int
    FileHandle::write(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      ELLE_ASSERT_EQ(buffer.size(), size);
      ELLE_DEBUG("write %s at %s on %s", size, offset, _owner._name);

      if (!_owner._multi() && size + offset > _owner.default_block_size)
        _owner._switch_to_multi(true);
      _dirty = true;
      if (!_owner._multi())
      {
        auto& block = _owner._first_block;
        if (offset + size > block->data().size())
        {
          int64_t old_size = block->data().size();
          block->data().size(offset + size);
          if (old_size < offset)
            memset(block->data().mutable_contents() + old_size, 0, offset - old_size);
        }
        memcpy(block->data().mutable_contents() + offset, buffer.mutable_contents(), size);
        // Update but do not commit yet, so that read on same fd do not fail.
        _owner._parent->_files.at(_owner._name).size += size;
        return size;
      }
      // multi mode
      uint64_t block_size = _owner._header().block_size;
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      {
        Block* block;
        auto const it = _owner._blocks.find(start_block);
        if (it != _owner._blocks.end())
        {
          block = it->second.block.get();
          it->second.dirty = true;
          it->second.last_use = std::chrono::system_clock::now();
        }
        else
        {
          bool change;
          boost::optional<Address> addr = _owner._block_address(start_block, true, &change);
          auto it_insert = _owner._blocks.insert(std::make_pair(start_block,
            File::CacheEntry{_owner._owner.block_store()->fetch(*addr), true}));
          block = it_insert.first->second.block.get();
          it_insert.first->second.last_use = std::chrono::system_clock::now();
          _owner.check_cache();
        }
        off_t block_offset = offset % block_size;
        bool growth = false;
        if (block->data().size() < block_offset + size)
        {
          growth = true;
          int64_t old_size = block->data().size();
          block->data().size(block_offset + size);
          if (old_size < block_offset)
          { // fill with zeroes
            memset(block->data().mutable_contents() + old_size, 0, block_offset - old_size);
          }
        }
        memcpy(&block->data()[block_offset], buffer.mutable_contents(), size);
        if (growth)
        { // check if file size was increased
          File::Header h = _owner._header();
          if (h.total_size < offset + size)
          {
            h.total_size = offset + size;
            _owner._header(h);
          }
        }
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
      File::Header h = _owner._header();
      h.total_size += r1+r2;
      _owner._header(h);
      // Assuming linear writes, this is a good time to flush start block since
      // it just got filled
      File::CacheEntry& ent = _owner._blocks.at(start_block);
      _owner._owner.block_store()->store(*ent.block);
      ent.dirty = false;
      return r1 + r2;
    }

    void
    FileHandle::ftruncate(off_t offset)
    {
      return _owner.truncate(offset);
    }
  }
}
