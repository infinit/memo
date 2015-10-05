#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/MissingBlock.hh>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/unordered_map.hh>

#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/SerializerOut.hh>

#include <reactor/filesystem.hh>
#include <reactor/scheduler.hh>
#include <reactor/exception.hh>

#include <cryptography/hash.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/serialization.hh>

#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif

ELLE_LOG_COMPONENT("infinit.fs");

namespace rfs = reactor::filesystem;

namespace infinit
{
  namespace filesystem
  {

    template<typename F> auto umbrella(F f, int err = EIO) -> decltype(f())
    {
      try {
        return f();
      }
      catch(reactor::Terminate const& e)
      {
        throw;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception %s", e);
        throw rfs::Error(EACCES, elle::sprintf("%s", e));
      }
      catch (rfs::Error const& e)
      {
        ELLE_TRACE("rethrowing rfs exception: %s", e);
        throw;
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("unexpected elle::exception %s", e);
        throw rfs::Error(err, elle::sprintf("%s", e));
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("unexpected std::exception %s", e);
        throw rfs::Error(err, e.what());
      }
    }

    typedef infinit::model::blocks::Block Block;
    typedef infinit::model::blocks::MutableBlock MutableBlock;
    typedef infinit::model::blocks::ImmutableBlock ImmutableBlock;
    typedef infinit::model::blocks::ACLBlock ACLBlock;
    typedef infinit::model::Address Address;

    static const int header_size = sizeof(Address::Value);
    static_assert(sizeof(Address) == header_size, "Glitch in Address size");

    enum class OperationType
    {
      insert,
      update,
      remove
    };
    struct Operation
    {
      OperationType type;
      std::string target;
    };

    class AnyBlock
    {
    public:
      AnyBlock();
      AnyBlock(std::unique_ptr<Block> block);
      AnyBlock(AnyBlock && b);
      AnyBlock(AnyBlock const& b) = delete;
      AnyBlock& operator = (const AnyBlock& b) = delete;
      void operator = (AnyBlock && b);
      Address address() {return _address;}
      void
      data(std::function<void (elle::Buffer&)> transformation);
      const elle::Buffer& data();
      // Output a block of same type, address might change
      std::unique_ptr<Block> take(infinit::model::Model& model);
      Address store(infinit::model::Model& model, infinit::model::StoreMode mode);

      void zero(int offset, int count);
      void write(int offset, const void* input, int count);
      void read(int offset, void* output, int count);
      std::unique_ptr<Block> _backend;
      bool _is_mutable;
      elle::Buffer _buf;
      Address _address;
    };

    class Directory;
    class File;
    typedef std::shared_ptr<Directory> DirectoryPtr;

    static Address::Value zeros = {0};
    struct FileData
    {
      std::string name;
      uint64_t size;
      uint32_t mode;
      uint32_t uid;
      uint32_t gid;
      uint64_t atime; // access:  read,
      uint64_t mtime; // content change  dir: create/delete file
      uint64_t ctime; //attribute change+content change
      Address address;
      boost::optional<std::string> symlink_target;
      std::unordered_map<std::string, elle::Buffer> xattrs;
      typedef infinit::serialization_tag serialization_tag;

      FileData(std::string name, uint64_t size, uint32_t mode, uint64_t atime,
        uint64_t mtime, uint64_t ctime, Address const& address,
        std::unordered_map<std::string, elle::Buffer> xattrs)
        : name(name)
        , size(size)
        , mode(mode)
        , atime(atime)
        , mtime(mtime)
        , ctime(ctime)
        , address(address)
        , xattrs(std::move(xattrs))
      {}

      FileData()
        : size(0)
        , mode(0)
        , uid(0)
        , gid(0)
        , atime(0)
        , mtime(0)
        , ctime(0)
        , address(zeros)
      {}

      FileData(elle::serialization::SerializerIn& s)
        : address(zeros)
      {
        s.serialize_forward(*this);
      }

      void serialize(elle::serialization::Serializer& s)
      {
        s.serialize("name", name);
        s.serialize("size", size);
        s.serialize("mode", mode);
        s.serialize("atime", atime);
        s.serialize("mtime", mtime);
        s.serialize("ctime", ctime);
        s.serialize("address", address);
        try {
          s.serialize("uid", uid);
          s.serialize("gid", gid);
        }
        catch(elle::serialization::Error const& e)
        {
          ELLE_WARN("serialization error %s, assuming old format", e);
        }
        s.serialize("symlink_target", symlink_target);
        s.serialize("xattrs", xattrs);
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
    #define THROW_ISDIR { throw rfs::Error(EISDIR, "Is a directory");}
    #define THROW_NOTDIR { throw rfs::Error(ENOTDIR, "Is not a directory");}
    #define THROW_NODATA { throw rfs::Error(ENODATA, "No data");}
    #define THROW_NOATTR { throw rfs::Error(ENOATTR, "No attribute");}
    #define THROW_INVAL { throw rfs::Error(EINVAL, "Invalid argument");}
    #define THROW_ACCES { throw rfs::Error(EACCES, "Access denied");}

    class Node
      : public elle::Printable
    {
    public:
      typedef infinit::serialization_tag serialization_tag;
    protected:
      Node(FileSystem& owner, std::shared_ptr<Directory> parent, std::string const& name)
      : _owner(owner)
      , _parent(parent)
      , _name(name)
      {}
      void rename(boost::filesystem::path const& where);
      void utimens(const struct timespec tv[2]);
      void chmod(mode_t mode);
      void chown(int uid, int gid);
      void stat(struct stat* st);
      std::string getxattr(std::string const& key);
      void setxattr(std::string const& k, std::string const& v, int flags);
      void removexattr(std::string const& k);
      std::unique_ptr<Block> set_permissions(std::string const& flags, std::string const& userkey,
                                             Address self_address);
      void _remove_from_cache(boost::filesystem::path p = boost::filesystem::path());
      std::unique_ptr<infinit::model::User> _get_user(std::string const& value);
      boost::filesystem::path full_path();
      FileSystem& _owner;
      std::shared_ptr<Directory> _parent;
      std::string _name;
    };

    class Unknown
      : public Node
      , public rfs::Path
    {
    public:
      Unknown(DirectoryPtr parent, FileSystem& owner, std::string const& name);
      void
      stat(struct stat*) override;
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
      std::shared_ptr<Path> child(std::string const& name) override THROW_NOENT;
      bool allow_cache() override { return false;}
      std::string getxattr(std::string const& k) override {THROW_NODATA;}
      virtual
      void
      print(std::ostream& stream) const override;
    private:
    };

    class Symlink
      : public Node
      , public rfs::Path
    {
    public:
      Symlink(DirectoryPtr parent, FileSystem& owner, std::string const& name);
      void stat(struct stat*) override;
      void list_directory(rfs::OnDirectoryEntry cb) THROW_NOTDIR;
      std::unique_ptr<rfs::Handle> open(int flags, mode_t mode) override THROW_NOSYS;
      std::unique_ptr<rfs::Handle> create(int flags, mode_t mode) override THROW_NOSYS;
      void unlink() override;
      void mkdir(mode_t mode) override THROW_EXIST;
      void rmdir() override THROW_NOTDIR;
      void rename(boost::filesystem::path const& where) override;
      boost::filesystem::path readlink() override;
      void symlink(boost::filesystem::path const& where) THROW_EXIST;
      void link(boost::filesystem::path const& where) override; //copied symlink
      void chmod(mode_t mode) override THROW_NOSYS; // target
      void chown(int uid, int gid) override THROW_NOSYS; // target
      void statfs(struct statvfs *) override THROW_NOSYS;
      void utimens(const struct timespec tv[2]) THROW_NOSYS;
      void truncate(off_t new_size) override THROW_NOSYS;
      std::shared_ptr<Path> child(std::string const& name) override THROW_NOTDIR;
      bool allow_cache() override { return false;}
      virtual
      void
      print(std::ostream& stream) const override;
    };

    class Directory
      : public rfs::Path
      , public Node
    {
    public:
      Directory(DirectoryPtr parent, FileSystem& owner, std::string const& name,
                Address address);
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
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]) override;
      void truncate(off_t new_size) override THROW_ISDIR;
      std::shared_ptr<rfs::Path> child(std::string const& name) override;
      std::string getxattr(std::string const& key) override;
      std::vector<std::string> listxattr() override;
      void setxattr(std::string const& name, std::string const& value, int flags) override;
      void removexattr(std::string const& name) override;
      void cache_stats(CacheStats& append);
      void serialize(elle::serialization::Serializer&);
      bool allow_cache() override { return true;}
      virtual
      void
      print(std::ostream& stream) const override;

    private:
      void _fetch();
      void move_recurse(boost::filesystem::path const& current,
                        boost::filesystem::path const& where);
      friend class Unknown;
      friend class File;
      friend class Symlink;
      friend class Node;
      friend class FileHandle;
      void _commit(Operation op, bool set_mtime = false);
      void _push_changes(Operation op, bool first_write = false);
      Address _address;
      std::unique_ptr<ACLBlock> _block;
      elle::unordered_map<std::string, FileData> _files;
      bool _inherit_auth; //child nodes inherit this dir's permissions
      boost::posix_time::ptime _last_fetch;
      friend class FileSystem;
    };

    static const boost::posix_time::time_duration directory_cache_time
      = boost::posix_time::seconds(2);

    class FileHandle
      : public rfs::Handle
      , public elle::Printable
    {
    public:
      FileHandle(std::shared_ptr<File> owner,
                 bool update_folder_mtime=false,
                 bool no_prefetch = false,
                 bool mark_dirty = false);
      ~FileHandle();
      virtual
      int
      read(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      virtual
      int
      write(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      virtual
      void
      ftruncate(off_t offset) override;
      virtual
      void
      fsync(int datasync) override;
      virtual
      void
      fsyncdir(int datasync) override;
      virtual
      void
      close() override;
      virtual
      void
      print(std::ostream& stream) const override;
    private:
      int
      _write_single(elle::WeakBuffer buffer, off_t offset);
      int
      _write_multi_single(elle::WeakBuffer buffer, off_t offset, int block);
      int
      _write_multi_multi(elle::WeakBuffer buffer, off_t offset,
                         int start_block, int end_block);
      ELLE_ATTRIBUTE(std::shared_ptr<File>, owner);
      ELLE_ATTRIBUTE(bool, dirty);
    };

    class File
      : public rfs::Path
      , public Node
    {
    public:
      File(DirectoryPtr parent, FileSystem& owner, std::string const& name,
                std::unique_ptr<MutableBlock> b = std::unique_ptr<MutableBlock>());
      ~File();
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
      void chmod(mode_t mode) override;
      void chown(int uid, int gid) override;
      void statfs(struct statvfs *) override;
      void utimens(const struct timespec tv[2]);
      void truncate(off_t new_size) override;
      std::string getxattr(std::string const& name) override;
      std::vector<std::string> listxattr() override;
      void setxattr(std::string const& name, std::string const& value, int flags) override;
      void removexattr(std::string const& name) override;
      std::shared_ptr<Path> child(std::string const& name) override THROW_NOTDIR;
      bool allow_cache() override;
      // check cached data size, remove entries if needed
      void check_cache();
      virtual
      void
      print(std::ostream& output) const override;

    private:
      static const unsigned long default_block_size;
      static const unsigned long max_cache_size; // in blocks
      friend class FileHandle;
      friend class Directory;
      friend class Unknown;
      friend class Node;
      // A packed network-byte-ordered version of Header sits at the
      // beginning of each file's first block in index mode.
      struct Header
      { // max size we can grow is sizeof(Address)
        static const uint32_t current_version = 1;
        bool     is_bare; // true if bare data below, false if block address table
        uint32_t version;
        uint32_t block_size;
        uint32_t links;
        uint64_t total_size;
      };
      /* Get address for given block index.
       * @param create: if true, allow creation of a new block as needed
       *                else returns nullptr if creation was required
      */
      AnyBlock*
      _block_at(int index, bool create);
      // Switch from direct to indexed mode
      void _switch_to_multi(bool alloc_first_block);
      void _ensure_first_block();
      void _commit();
      Header _header(); // Get header, must be in multi mode
      void _header(Header const&);
      bool _multi(); // True if mode is index
      struct CacheEntry
      {
        AnyBlock block;
        bool dirty;
        std::chrono::system_clock::time_point last_use;
        bool new_block;
      };
      std::unordered_map<int, CacheEntry> _blocks;
      std::unique_ptr<MutableBlock> _first_block;
      bool _first_block_new;
      int _handle_count;
      boost::filesystem::path _full_path;
    };

    const unsigned long File::default_block_size = 1024 * 1024;
    const unsigned long File::max_cache_size = 20; // in blocks

    AnyBlock::AnyBlock()
    {}
    AnyBlock::AnyBlock(std::unique_ptr<Block> block)
    : _backend(std::move(block))
    , _is_mutable(dynamic_cast<MutableBlock*>(_backend.get()))
    {
      _address = _backend->address();
      ELLE_DEBUG("Anyblock mutable=%s, addr = %x", _is_mutable, _backend->address());
      if (!_is_mutable)
      {
        _buf = _backend->take_data();
        ELLE_DEBUG("Nonmutable, stole %s bytes", _buf.size());
      }
    }

    AnyBlock::AnyBlock(AnyBlock && b)
    : _backend(std::move(b._backend))
    , _is_mutable(b._is_mutable)
    , _buf(std::move(b._buf))
    , _address(b._address)
    {

    }
    void AnyBlock::operator = (AnyBlock && b)
    {
      _backend = std::move(b._backend);
     _is_mutable = b._is_mutable;
     _buf = std::move(b._buf);
     _address = b._address;
    }
    void AnyBlock::data(std::function<void (elle::Buffer&)> transformation)
    {
      if (_is_mutable)
        dynamic_cast<MutableBlock*>(_backend.get())->data(transformation);
      else
        transformation(_buf);
    }
    elle::Buffer const& AnyBlock::data()
    {
      if (_is_mutable)
        return _backend->data();
      else
        return _buf;
    }
    void AnyBlock::zero(int offset, int count)
    {
      data([&](elle::Buffer& data)
        {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memset(data.mutable_contents() + offset, 0, count);
        });
    }
    void AnyBlock::write(int offset, const void* input, int count)
    {
      data([&](elle::Buffer& data)
         {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memcpy(data.mutable_contents() + offset, input, count);
         });
    }
    void AnyBlock::read(int offset, void* output, int count)
    {
      data([&](elle::Buffer& data)
         {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memcpy(output, data.mutable_contents() + offset, count);
         });
    }
    std::unique_ptr<Block> AnyBlock::take(infinit::model::Model& model)
    {
      if (_is_mutable)
        return std::move(_backend);
      else
        return model.make_block<ImmutableBlock>(std::move(_buf));
    }

    Address AnyBlock::store(infinit::model::Model& model,
                            infinit::model::StoreMode mode)
    {
      ELLE_DEBUG("Anyblock store: %x", _backend->address());
      if (_is_mutable)
      {
        umbrella([&] {model.store(*_backend, mode);});
        return _backend->address();
      }
      auto block = model.make_block<ImmutableBlock>(_buf);
      umbrella([&] { model.store(*block, mode);});
      _address = block->address();
      return _address;
    }

    FileSystem::FileSystem(std::string const& volume_name,
                           std::shared_ptr<model::Model> model)
      : _block_store(std::move(model))
      , _single_mount(false)
      , _volume_name(volume_name)
    {
      reactor::scheduler().signal_handle
        (SIGUSR1, [this] { this->print_cache_stats();});
    }

    void
    FileSystem::unchecked_remove(model::Address address)
    {
      try
      {
        _block_store->remove(address);
      }
      catch (model::MissingBlock const&)
      {
        ELLE_DEBUG("%s: block was not published", *this);
      }
      catch (elle::Exception const& e)
      {
        ELLE_ERR("%s: unexpected exception: %s", *this, e.what());
        throw;
      }
      catch (...)
      {
        ELLE_ERR("%s: unknown exception", *this);
        throw;
      }
    }

    void
    FileSystem::store_or_die(model::blocks::Block& block, model::StoreMode mode)
    {
      ELLE_TRACE_SCOPE("%s: store or die: %s", *this, block);

      try
      {
        this->_block_store->store(block, mode);
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("permission exception: %s", e.what());
        throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
      }
      catch(elle::Error const& e)
      {
        ELLE_WARN("unexpected exception storing %x: %s",
                  block.address(), e);
        throw rfs::Error(EIO, e.what());
      }
    }

    std::unique_ptr<model::blocks::Block>
    FileSystem::fetch_or_die(model::Address address)
    {
      try
      {
        return _block_store->fetch(address);
      }
      catch(reactor::Terminate const& e)
      {
        throw;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception %s", e);
        throw rfs::Error(EACCES, elle::sprintf("%s", e));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("unexpected storage result fetching: %s", mb);
        throw rfs::Error(EIO, elle::sprintf("%s", mb));
      }
      catch (elle::serialization::Error const& se)
      {
        ELLE_WARN("serialization error fetching %x: %s", address, se);
        throw rfs::Error(EIO, elle::sprintf("%s", se));
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("unexpected exception fetching %x: %s", address, e);
        throw rfs::Error(EIO, elle::sprintf("%s", e));
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("unexpected exception on fetching %x: %s", address, e);
        throw rfs::Error(EIO, e.what());
      }
    }

    std::unique_ptr<model::blocks::MutableBlock>
    FileSystem::unchecked_fetch(model::Address address)
    {
      try
      {
        return elle::cast<model::blocks::MutableBlock>::runtime
          (_block_store->fetch(address));
      }
      catch (model::MissingBlock const& mb)
      {
        ELLE_WARN("Unexpected storage result: %s", mb);
      }
      return {};
    }

    void
    FileSystem::print_cache_stats()
    {
      auto root = std::dynamic_pointer_cast<Directory>(filesystem()->path("/"));
      CacheStats stats;
      memset(&stats, 0, sizeof(CacheStats));
      root->cache_stats(stats);
      std::cerr << "Statistics:\n"
      << stats.directories << " dirs\n"
      << stats.files << " files\n"
      << stats.blocks <<" blocks\n"
      << stats.size << " bytes"
      << std::endl;
    }

    std::shared_ptr<rfs::Path>
    FileSystem::path(std::string const& path)
    {
      ELLE_TRACE_SCOPE("%s: fetch root", *this);
      // In the infinit filesystem, we never query a path other than the root.
      ELLE_ASSERT_EQ(path, "/");
      auto root = this->_root_block();
      ELLE_ASSERT(!!root);
      auto acl_root =  elle::cast<ACLBlock>::runtime(std::move(root));
      ELLE_ASSERT(!!acl_root);
      auto res =
        std::make_shared<Directory>(nullptr, *this, "", acl_root->address());
      res->_block = std::move(acl_root);
      return res;
    }

    std::unique_ptr<MutableBlock>
    FileSystem::_root_block()
    {
      auto dn =
        std::dynamic_pointer_cast<model::doughnut::Doughnut>(_block_store);
      Address addr =
        model::doughnut::NB::address(dn->owner(), _volume_name + ".root");
      while (true)
      {
        try
        {
          ELLE_DEBUG_SCOPE("fetch root boostrap block at %x", addr);
          auto block = _block_store->fetch(addr);
          addr = Address::from_string(block->data().string().substr(2));
          break;
        }
        catch (model::MissingBlock const& e)
        {
          if (dn->owner() == dn->keys().K())
          {
            ELLE_TRACE("create missing root bootsrap block");
            std::unique_ptr<MutableBlock> mb = dn->make_block<ACLBlock>();
            auto saddr = elle::sprintf("%x", mb->address());
            elle::Buffer baddr = elle::Buffer(saddr.data(), saddr.size());
            store_or_die(*mb, model::STORE_INSERT);
            model::doughnut::NB nb(dn.get(), dn->owner(),
                                   this->_volume_name + ".root", baddr);
            store_or_die(nb, model::STORE_INSERT);
            return mb;
          }
          reactor::sleep(1_sec);
        }
      }
      return elle::cast<MutableBlock>::runtime(fetch_or_die(addr));
    }

    static const int DIRECTORY_MASK = 0040000;
    static const int SYMLINK_MASK = 0120000;

    void
    Directory::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("content", this->_files);
      s.serialize("inherit_auth", this->_inherit_auth);
    }

    Directory::Directory(DirectoryPtr parent, FileSystem& owner,
                         std::string const& name,
                         Address address)
      : Node(owner, parent, name)
      , _address(address)
      , _inherit_auth(_parent?_parent->_inherit_auth : false)
      , _last_fetch(boost::posix_time::not_a_date_time)
    {}

    void Directory::_fetch()
    {
      auto now = boost::posix_time::microsec_clock::universal_time();
      if (_block && _last_fetch != boost::posix_time::not_a_date_time
        && now - _last_fetch < directory_cache_time)
      {
        ELLE_DUMP("using directory cache");
        return;
      }
      _block = elle::cast<ACLBlock>::runtime
        (_owner.fetch_or_die(_address));
      std::unordered_map<std::string, FileData> local;
      std::swap(local, _files);
      bool empty = false;
      elle::IOStream is(umbrella([&] {
          auto& d = _block->data();
          empty = d.empty();
          return d.istreambuf();
        }
        , EACCES));
      if (empty)
      {
        _last_fetch = now;
        return;
      }
      elle::serialization::json::SerializerIn input(is);
      try
      {
        input.serialize_forward(*this);
      }
      catch(elle::serialization::Error const& e)
      {
        ELLE_WARN("Directory deserialization error: %s", e);
        std::swap(local, _files);
        throw rfs::Error(EIO, e.what());
      }
      // File writes update the file size in _files for reads to work,
      // but they do not commit them to store (that would be far too expensive)
      // So, keep local version of entries with bigger ctime than remote.
      for (auto const& itl: local)
      {
        auto itr = _files.find(itl.first);
        if (itr != _files.end()
          && (itr->second.ctime < itl.second.ctime
              || itr->second.mtime < itl.second.mtime))
        {
          ELLE_DEBUG("Using local data for %s", itl.first);
          itr->second = itl.second;
        }
      }
      _last_fetch = now;
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
    Directory::_commit(Operation op, bool set_mtime)
    {
      ELLE_TRACE_SCOPE("%s: commit %s entries", *this, _files.size());
      elle::SafeFinally clean_cache([&] { _block.reset();});
      elle::Buffer data;
      {
        elle::IOStream os(data.ostreambuf());
        elle::serialization::json::SerializerOut output(os);
        output.serialize_forward(*this);
      }
      ELLE_DUMP("content: %s", data);
      if (!_block)
	ELLE_DEBUG("fetch root block")
	  _block = elle::cast<ACLBlock>::runtime(_owner.fetch_or_die(_address));
      _block->data(data);
      if (set_mtime && _parent)
      {
	ELLE_DEBUG_SCOPE("set mtime");
        FileData& f = _parent->_files.at(_name);
        f.mtime = time(nullptr);
        f.ctime = time(nullptr);
        f.address = _block->address();
        this->_parent->_commit({OperationType::update, _name});
      }
      this->_push_changes(op);
      clean_cache.abort();
    }

    void
    Directory::_push_changes(Operation op, bool first_write)
    {
      ELLE_DEBUG_SCOPE("%s: store changes", *this);
      elle::SafeFinally clean_cache([&] { _block.reset();});
      while (true)
      {
        try
        {
          _owner.block_store()->store(*_block, first_write ? model::STORE_INSERT : model::STORE_ANY);
          break;
        }
        catch (infinit::model::doughnut::Conflict const& e)
        {
          ELLE_TRACE("%s: edit conflict on %s (%s %s): %s",
                     *this, _block->address(), op.type, op.target, e);
          // Fetch and deserialize live block
          _last_fetch = boost::posix_time::not_a_date_time;
          Directory d(_parent, _owner, _name, _address);
          d._fetch();
          // Apply current operation to it
          switch(op.type)
          {
          case OperationType::insert:
            if (d._files.find(op.target) != d._files.end())
            {
              ELLE_LOG("Conflict: the object %s was also created remotely,"
                " your changes will overwrite the previous content.",
                full_path() / op.target);
            }
            d._files[op.target] = _files[op.target];
            break;
          case OperationType::update:
            if (d._files.find(op.target) == d._files.end())
            {
              ELLE_LOG("Conflict: the object %s was removed remotely,"
                " your changes will be dropped.",
                full_path() / op.target);
              break;
            }
            else if (d._files[op.target].address != _files[op.target].address)
            {
              ELLE_LOG("Conflict: the object %s was replaced remotely,"
                " your changes will be dropped.",
                full_path() / op.target);
              break;
            }
            d._files[op.target] = _files[op.target];
            break;
          case OperationType::remove:
            d._files.erase(op.target);
            break;
          }
          elle::Buffer data;
          {
            elle::IOStream os(data.ostreambuf());
            elle::serialization::json::SerializerOut output(os);
            output.serialize_forward(d);
          }
          d._block->data(data);
          _block = std::move(d._block);
        }
        catch (infinit::model::doughnut::ValidationFailed const& e)
        {
          ELLE_TRACE("permission exception: %s", e.what());
          throw rfs::Error(EACCES, elle::sprintf("%s", e.what()));
        }
        catch(elle::Error const& e)
        {
          ELLE_WARN("unexpected exception storing %x: %s",
            _block->address(), e);
          throw rfs::Error(EIO, e.what());
        }
      }
      //_owner.store_or_die(*_block, first_write ? model::STORE_INSERT : model::STORE_ANY);
      clean_cache.abort();
    }

    std::shared_ptr<rfs::Path>
    Directory::child(std::string const& name)
    {
      ELLE_TRACE_SCOPE("%s: get child \"%s\"", *this, name);
      if (!_owner.single_mount() || !_block)
        _fetch();
      auto it = _files.find(name);
      auto self = std::dynamic_pointer_cast<Directory>(shared_from_this());
      if (it != _files.end())
      {
        bool isDir = signed(it->second.mode & S_IFMT)  == DIRECTORY_MASK;
        bool isSymlink = signed(it->second.mode & S_IFMT) == SYMLINK_MASK;
        if (isSymlink)
          return std::shared_ptr<rfs::Path>(new Symlink(self, _owner, name));
        if (!isDir)
          return std::shared_ptr<rfs::Path>(new File(self, _owner, name));

        return std::shared_ptr<rfs::Path>(new Directory(self, _owner, name,
                                                        it->second.address));
      }
      else
        return std::shared_ptr<rfs::Path>(new Unknown(self, _owner, name));
    }

    void
    Directory::list_directory(rfs::OnDirectoryEntry cb)
    {
      ELLE_TRACE_SCOPE("%s: list", *this);
      if (!_owner.single_mount() || ! _block)
        _fetch();
      struct stat st;
      for (auto const& e: _files)
      {
        if (e.first.empty())
          continue;
        st.st_mode = e.second.mode;
        st.st_size = e.second.size;
        st.st_atime = e.second.atime;
        st.st_mtime = e.second.mtime;
        st.st_ctime = e.second.ctime;
        cb(e.first, &st);
      }
    }

    void
    Directory::rmdir()
    {
      ELLE_TRACE_SCOPE("%s: remove", *this);
      _fetch();
      if (!_files.empty())
        throw rfs::Error(ENOTEMPTY, "Directory not empty");
      if (_parent.get() == nullptr)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      _parent->_files.erase(_name);
      _parent->_commit({OperationType::remove, _name});
      umbrella([&] {_owner.block_store()->remove(_block->address());});
      _remove_from_cache();
    }

    void
    Directory::move_recurse(boost::filesystem::path const& current,
                            boost::filesystem::path const& where)
    {
      for (auto const& v: _files)
      {
        std::string const& name = v.first;
        ELLE_DEBUG("Extracting %s", current / name);
        auto p = _owner.filesystem()->extract((current / name).string());
        if (p)
        {
          auto ptr = p.get();
          ELLE_DEBUG("Inserting %s", where / name);
          _owner.filesystem()->set((where/name).string(), std::move(p));
          if (signed(v.second.mode & S_IFMT) ==  DIRECTORY_MASK)
          {
            dynamic_cast<Directory*>(ptr)->move_recurse(current / name, where / name);
          }
        }
      }
    }

    void
    Directory::rename(boost::filesystem::path const& where)
    {
      boost::filesystem::path current = full_path();
      Node::rename(where);
      // We might have children that pointed to us, we need to move them
      this->move_recurse(current, where);
    }

    void
    Node::rename(boost::filesystem::path const& where)
    {
      boost::filesystem::path current = full_path();
      std::string newname = where.filename().string();
      boost::filesystem::path newpath = where.parent_path();
      if (!_parent)
        throw rfs::Error(EINVAL, "Cannot delete root node");
      auto dir = std::dynamic_pointer_cast<Directory>(
        _owner.filesystem()->path(newpath.string()));
      if (dir->_files.find(newname) != dir->_files.end())
      {
        ELLE_TRACE_SCOPE("%s: remove existing destination", *this);
        // File and empty dir gets removed.
        auto target = _owner.filesystem()->path(where.string());
        struct stat st;
        target->stat(&st);
        if (signed(st.st_mode & S_IFMT) == DIRECTORY_MASK)
        {
          try
          {
            target->rmdir();
          }
          catch(rfs::Error const& e)
          {
            throw rfs::Error(EISDIR, "Target is a directory");
          }
        }
        else
          target->unlink();
        ELLE_DEBUG("removed move target %s", where);
      }
      auto data = _parent->_files.at(_name);
      _parent->_files.erase(_name);
      _parent->_commit({OperationType::remove, _name});
      data.name = newname;
      dir->_files.insert(std::make_pair(newname, data));
      dir->_commit({OperationType::insert, _name});
      _name = newname;
      _parent = dir;
      // Move the node in cache
      ELLE_DEBUG("Extracting %s", current);
      auto p = _owner.filesystem()->extract(current.string());
      if (p)
      {
        std::dynamic_pointer_cast<Node>(p)->_name = newname;
        // This might delete the dummy Unknown on destination which is fine
        ELLE_DEBUG("Setting %s", where);
        _owner.filesystem()->set(where.string(), p);
      }
    }

    void
    Node::_remove_from_cache(boost::filesystem::path full_path)
    {
      if (full_path == boost::filesystem::path())
        full_path = this->full_path();
      ELLE_DEBUG("remove_from_cache: %s (%s) entering", _name, full_path);
      std::shared_ptr<rfs::Path> self = _owner.filesystem()->extract(full_path.string());
      ELLE_DEBUG("remove_from_cache: %s released (%s), this %s", _name, self, this);
      new reactor::Thread("delayed_cleanup", [self] { ELLE_DEBUG("async_clean");}, true);
      ELLE_DEBUG("remove_from_cache: %s exiting with async cleaner", _name);
    }

    boost::filesystem::path
    Node::full_path()
    {
      if (_parent == nullptr)
        return "/";
      return _parent->full_path() / _name;
    }

    void
    Directory::stat(struct stat* st)
    {
      ELLE_TRACE_SCOPE("%s: stat", *this);
      Node::stat(st);
      if (_parent)
      {
        try
        {
          mode_t mode = st->st_mode;
          st->st_mode &= ~0777;
          _block = elle::cast<ACLBlock>::runtime
            (_owner.fetch_or_die(_parent->_files.at(_name).address));
          _block->data();
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
      }
    }

    void
    Directory::cache_stats(CacheStats& cs)
    {
      cs.directories++;
      boost::filesystem::path current = full_path();
      for(auto const& f: _files)
      {
        auto p = _owner.filesystem()->get((current / f.second.name).string());
        if (!p)
          return;
        if (Directory* d = dynamic_cast<Directory*>(p.get()))
          d->cache_stats(cs);
        else if (File* f = dynamic_cast<File*>(p.get()))
        {
          cs.files++;
          cs.blocks += 1 + f->_blocks.size();
          if (f->_first_block)
            cs.size += f->_first_block->data().size();
          for (auto& b: f->_blocks)
            cs.size += b.second.block.data().size();
        }
      }
    }

    void
    Directory::chmod(mode_t mode)
    {
      Node::chmod(mode);
    }

    void
    File::chmod(mode_t mode)
    {
      Node::chmod(mode);
    }

    void
    Node::chmod(mode_t mode)
    {
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.mode = (f.mode & ~07777) | (mode & 07777);
      f.ctime = time(nullptr);
      _parent->_commit({OperationType::update, _name});
    }

    void
    Directory::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }

    void
    File::chown(int uid, int gid)
    {
      Node::chown(uid, gid);
    }

    void
    Node::chown(int uid, int gid)
    {
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.uid = uid;
      f.gid = gid;
      f.ctime = time(nullptr);
      _parent->_commit({OperationType::update, _name});
    }

    void File::removexattr(std::string const& k)
    {
      Node::removexattr(k);
    }

    void Directory::removexattr(std::string const& k)
    {
      Node::removexattr(k);
    }

    void
    Node::removexattr(std::string const& k)
    {
      auto& xattrs = _parent ?
         _parent->_files.at(_name).xattrs
         : static_cast<Directory*>(this)->_files[""].xattrs;
      ELLE_DEBUG("got xattrs with %s entries", xattrs.size());
      xattrs.erase(k);
      if (_parent)
        _parent->_commit({OperationType::update, _name},false);
      else
        static_cast<Directory*>(this)->_commit({OperationType::update, ""},  false);
    }

    void
    Node::setxattr(std::string const& k, std::string const& v, int flags)
    {
      /* Drop quarantine flags, preventing the files from being opened.
      * https://github.com/osxfuse/osxfuse/issues/162
      */
      if (k == "com.apple.quarantine")
        return;
      if (k.substr(0, strlen("user.infinit.overlay.")) == "user.infinit.overlay.")
      {
        std::string okey = k.substr(strlen("user.infinit.overlay."));
        umbrella([&] {
          dynamic_cast<model::doughnut::Doughnut*>(_owner.block_store().get())
            ->overlay()->query(okey, v);
        }, EINVAL);
        return;
      }
      auto& xattrs = _parent ?
         _parent->_files.at(_name).xattrs
         : static_cast<Directory*>(this)->_files[""].xattrs;
      ELLE_DEBUG("got xattrs with %s entries", xattrs.size());
      xattrs[k] = elle::Buffer(v.data(), v.size());
      if (_parent)
        _parent->_commit({OperationType::update, _name}, false);
      else
        static_cast<Directory*>(this)->_commit({OperationType::update, ""},false);
    }

    std::string
    Node::getxattr(std::string const& k)
    {
      ELLE_TRACE_SCOPE("%s: get attribute \"%s\"", *this, k);
      if (k.substr(0, strlen("user.infinit.overlay.")) == "user.infinit.overlay.")
      {
        std::string okey = k.substr(strlen("user.infinit.overlay."));
        elle::json::Json v = umbrella([&] {
          return dynamic_cast<model::doughnut::Doughnut*>(_owner.block_store().get())
            ->overlay()->query(okey, {});
        }, EINVAL);
        if (v.empty())
          return "{}";
        else
          return elle::json::pretty_print(v);
      }
      auto& xattrs = this->_parent
        ? this->_parent->_files.at(this->_name).xattrs
        : static_cast<Directory*>(this)->_files[""].xattrs;
      auto it = xattrs.find(k);
      if (it == xattrs.end())
      {
        ELLE_DEBUG("no such attribute");
        THROW_NOATTR;
      }
      std::string value = it->second.string();
      ELLE_DUMP("value: \"%s\"", it, value);
      return value;
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
        st->st_ctime = fd.ctime;
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

    Unknown::Unknown(DirectoryPtr parent, FileSystem& owner, std::string const& name)
      : Node(owner, parent, name)
    {}

    void
    Unknown::mkdir(mode_t mode)
    {
      ELLE_DEBUG("mkdir %s", _name);
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      auto address = b->address();
      if (_parent->_inherit_auth)
      {
        ELLE_ASSERT(!!_parent->_block);
        // We must store first to ready ACL layer
        _owner.store_or_die(*b, model::STORE_INSERT);
        umbrella([&] { _parent->_block->copy_permissions(*b);});
        Directory d(_parent, _owner, _name, address);
        d._block = std::move(b);
        d._inherit_auth = true;
        d._push_changes({OperationType::insert, ""});
      }
      else
        _owner.store_or_die(*b, model::STORE_INSERT);
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      _parent->_files.insert(
        std::make_pair(_name,
                       FileData{_name, 0, static_cast<uint32_t>(mode | DIRECTORY_MASK),
                                uint64_t(time(nullptr)),
                                uint64_t(time(nullptr)),
                                uint64_t(time(nullptr)),
                                address,
                                std::unordered_map<std::string, elle::Buffer>{}}));
      _parent->_commit({OperationType::insert, _name});
      _remove_from_cache();
    }

    std::unique_ptr<rfs::Handle>
    Unknown::create(int flags, mode_t mode)
    {
      ELLE_ASSERT_EQ(signed(mode & S_IFMT), S_IFREG);
      if (!_owner.single_mount())
        _parent->_fetch();
      if (_parent->_files.find(_name) != _parent->_files.end())
      {
        ELLE_WARN("File %s exists where it should not", _name);
        _remove_from_cache();
        auto f = std::dynamic_pointer_cast<File>(_owner.filesystem()->path(full_path().string()));
        return f->open(flags, mode);
      }
      auto b = _owner.block_store()->make_block<infinit::model::blocks::ACLBlock>();
      //optimize: dont push block yet _owner.block_store()->store(*b);
      ELLE_DEBUG("Adding file to parent %x", _parent.get());
      _parent->_files.insert(
        std::make_pair(_name, FileData{_name, 0, mode,
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       b->address(),
                                       std::unordered_map<std::string, elle::Buffer>{}}));

      _parent->_commit({OperationType::insert, _name}, true);
      elle::SafeFinally remove_from_parent( [&] {
          _parent->_files.erase(_name);
          try
          {
            _parent->_commit({OperationType::remove, _name}, true);
          }
          catch(...)
          {
            ELLE_WARN("Rollback failure on %s", _name);
          }
      });
      _remove_from_cache();
      auto raw = _owner.filesystem()->path(full_path().string());
      auto f = std::dynamic_pointer_cast<File>(raw);
      if (!f)
        ELLE_ERR("Expected valid pointer from %s, got nullptr", raw.get());
      f->_first_block = std::move(b);
      File::Header h;
      h.version = File::Header::current_version;
      h.is_bare = true;
      h.block_size = File::default_block_size;
      h.links = 1;
      h.total_size = 0;
      f->_header(h);
      if (_parent->_inherit_auth)
      {
        umbrella([&] { _parent->_block->copy_permissions(
          dynamic_cast<ACLBlock&>(*f->_first_block));
        });
      }
      _owner.store_or_die(*f->_first_block, model::STORE_INSERT);
      f->_first_block_new = false;
      _owner.filesystem()->set(f->full_path().string(), f);
      std::unique_ptr<rfs::Handle> handle(new FileHandle(f, true, true, true));
      remove_from_parent.abort();
      return handle;
    }

    void
    Unknown::symlink(boost::filesystem::path const& where)
    {
      ELLE_ASSERT(_parent->_files.find(_name) == _parent->_files.end());
      Address::Value v {0};
      auto it =_parent->_files.insert(
        std::make_pair(_name, FileData{_name, 0, 0777 | SYMLINK_MASK,
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       uint64_t(time(nullptr)),
                                       Address(v),
                                       std::unordered_map<std::string, elle::Buffer>{}}));
      it.first->second.symlink_target = where.string();
      _parent->_commit({OperationType::insert, _name}, true);
      _remove_from_cache();
    }

    void
    Unknown::link(boost::filesystem::path const& where)
    {
      throw rfs::Error(ENOENT, "link source does not exist");
    }

    void
    Unknown::stat(struct stat*)
    {
      ELLE_TRACE_SCOPE("%s: stat", *this);
      THROW_NOENT;
    }

    void
    Unknown::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Unknown(\"%s\")", this->_name);
    }

    Symlink::Symlink(DirectoryPtr parent,
                     FileSystem& owner,
                     std::string const& name)
      : Node(owner, parent, name)
    {}

    void
    Symlink::stat(struct stat* s)
    {
      ELLE_TRACE_SCOPE("%s: stat", *this);
      Node::stat(s);
    }

    void
    Symlink::unlink()
    {
      _parent->_files.erase(_name);
      _parent->_commit({OperationType::remove, _name},true);
      _remove_from_cache();
    }

    void
    Symlink::rename(boost::filesystem::path const& where)
    {
      Node::rename(where);
    }

    boost::filesystem::path
    Symlink::readlink()
    {
      return *_parent->_files.at(_name).symlink_target;
    }

    void
    Symlink::link(boost::filesystem::path const& where)
    {
      auto p = _owner.filesystem()->path(where.string());
      Unknown* unk = dynamic_cast<Unknown*>(p.get());
      if (unk == nullptr)
        THROW_EXIST;
      unk->symlink(readlink());
    }

    void
    Symlink::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Symlink(\"%s\")", this->_name);
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
        _first_block = elle::cast<MutableBlock>::runtime
          (_owner.fetch_or_die(_parent->_files.at(_name).address));
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
      if (!_handle_count)
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
      }
      bool multi = umbrella([&] { return _multi();});
      if (_parent)
      {
        _parent->_files.erase(_name);
        _parent->_commit({OperationType::remove, _name}, true);
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
          _owner.store_or_die(*_first_block);
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
          _first_block = elle::cast<MutableBlock>::runtime
        (_owner.fetch_or_die(_parent->_files.at(_name).address));
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
    Directory::utimens(const struct timespec tv[2])
    {
      Node::utimens(tv);
    }

    void
    Node::utimens(const struct timespec tv[2])
    {
      ELLE_TRACE_SCOPE("%s: utimens: %s", *this, tv);
      if (!_parent)
        return;
      auto & f = _parent->_files.at(_name);
      f.atime = tv[0].tv_sec;
      f.mtime = tv[1].tv_sec;
      _parent->_commit({OperationType::update, _name});
    }

    void
    File::truncate(off_t new_size)
    {
      if (!_handle_count)
        _first_block = elle::cast<MutableBlock>::runtime
          (_owner.fetch_or_die(_parent->_files.at(_name).address));
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
          _owner.store_or_die(*_first_block);
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
          _first_block = elle::cast<MutableBlock>::runtime(
            _owner.fetch_or_die(addr));
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
              b.second.new_block = true;
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
        while (true)
        {
          try
          {
            _owner.block_store()->store( *this->_first_block,
              this->_first_block_new ? model::STORE_INSERT : model::STORE_ANY);
            break;
          }
          catch (infinit::model::doughnut::Conflict const& e)
          {
            ELLE_TRACE("%s: edit conflict on %s: %s", *this,
                       this->_first_block->address(), e);
            ELLE_LOG("Conflict: the file %s was modified since last read. Your"
              " changes will overwrite the previous modifications",
              full_path());
            auto block = elle::cast<MutableBlock>::runtime(
              _owner.block_store()->fetch(this->_first_block->address()));
            block->data(this->_first_block->data());
            this->_first_block = std::move(block);
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
          it->second.new_block = false;
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
            it->second.new_block = true;
          }
        }
        _blocks.erase(it);
      }
      if (fat_change)
      {
        _owner.store_or_die(*_first_block,
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
    std::vector<std::string> Directory::listxattr()
    {
      ELLE_TRACE("directory listxattr");
      std::vector<std::string> res;
      if (!_parent)
      {
        auto it = _files.find("");
        if (it == _files.end())
          return res;
        for (auto const& a: it->second.xattrs)
          res.push_back(a.first);
        return res;
      }
      for (auto const& a: _parent->_files.at(_name).xattrs)
        res.push_back(a.first);
      return res;
    }

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

    std::unique_ptr<infinit::model::User>
    Node::_get_user(std::string const& value)
    {
      if (value.empty())
        THROW_INVAL;
      ELLE_TRACE("setxattr raw key");
      elle::Buffer userkey = elle::Buffer(value.data(), value.size());
      auto user = _owner.block_store()->make_user(userkey);
      return std::move(user);
    }
    static std::pair<bool, bool> parse_flags(std::string const& flags)
    {
      bool r = false;
      bool w = false;
      if (flags == "clear")
        ;
      else if (flags == "setr")
        r = true;
      else if (flags == "setw")
        w = true;
      else if (flags == "setrw")
      {
        r = true; w = true;
      }
      else
        THROW_NODATA;
      return std::make_pair(r, w);
    }

    std::unique_ptr<Block>
    Node::set_permissions(std::string const& flags, std::string const& userkey,
                          Address self_address)
    {
      std::pair<bool, bool> perms = parse_flags(flags);
      std::unique_ptr<infinit::model::User> user =
        umbrella([&] {return _get_user(userkey);}, EINVAL);
      if (!user)
      {
        ELLE_WARN("user %s does not exist", userkey);
        THROW_INVAL;
      }
      auto block = _owner.fetch_or_die(self_address);
      auto acl = dynamic_cast<model::blocks::ACLBlock*>(block.get());
      if (!acl)
        throw rfs::Error(EIO, "Block is not an ACL block");
      // permission check
      auto acb = dynamic_cast<model::doughnut::ACB*>(block.get());
      if (!acb)
        throw rfs::Error(EIO, "Block is not an ACB block");
      auto dn =
        std::dynamic_pointer_cast<model::doughnut::Doughnut>(_owner.block_store());
      auto keys = dn->keys();
      if (keys.K() != acb->owner_key())
        THROW_ACCES;
      ELLE_TRACE("Setting permission at %s for %s", acl->address(), user->name());
      umbrella([&] {acl->set_permissions(*user, perms.first, perms.second);},
        EACCES);
      _owner.store_or_die(*acl);
      return block;
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

    void Directory::setxattr(std::string const& name, std::string const& value, int flags)
    {
      ELLE_TRACE("directory setxattr %s", name);
      _fetch();
      if (name == "user.infinit.auth.inherit")
      {
        bool on = !(value == "0" || value == "false" || value=="");
        _inherit_auth = on;
        _commit({OperationType::update, ""});
      }
      else if (name.find("user.infinit.auth.") == 0)
      {
        _block = elle::cast<ACLBlock>::runtime(
          set_permissions(name.substr(strlen("user.infinit.auth.")), value,
                          _block->address()));
      }
      else if (name == "user.infinit.fsck.unlink")
      {
        auto it = _files.find(value);
        if (it == _files.end())
          THROW_NOENT;
        auto c = child(value);
        auto f = dynamic_cast<File*>(c.get());
        if (!f)
          THROW_ISDIR;
        try
        {
          f->unlink();
        }
        catch(std::exception const& e)
        {
          ELLE_WARN("%s: unlink of %s failed with %s, forcibly remove from parent",
                    *this, value, e.what());
          _files.erase(value);
          _commit({OperationType::remove, value}, true);
        }
      }
      else
        Node::setxattr(name, value, flags);
    }

    std::string
    Directory::getxattr(std::string const& key)
    {
      if (key == "user.infinit.block")
      {
        if (_block)
          return elle::sprintf("%x", _block->address());
        else if (_parent)
        {
          auto const& elem = _parent->_files.at(_name);
          return elle::sprintf("%x", elem.address);
        }
        else
          return "<ROOT>";
      }
      else if (key == "user.infinit.auth")
      {
        _fetch();
        return perms_to_json(*_block);
      }
      else if (key == "user.infinit.auth.inherit")
      {
        _fetch();
        return _inherit_auth ? "true" : "false";
      }
      else
        return Node::getxattr(key);
    }

    void
    Directory::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Directory(\"/%s\")", this->_name);
    }

    FileHandle::FileHandle(std::shared_ptr<File> owner,
                           bool push_mtime,
                           bool no_fetch,
                           bool dirty)
      : _owner(owner)
      , _dirty(dirty)
    {
      ELLE_TRACE_SCOPE("%s: create (previous handle count = %s)",
                       *this, _owner->_handle_count);
      _owner->_handle_count++;
      _owner->_parent->_fetch();
      _owner->_parent->_files.at(_owner->_name).atime = time(nullptr);
      // This atime implementation does not honor noatime option
#if false
      try
      {
        _owner->_parent->_commit(push_mtime);
      }
      catch (std::exception const& e)
      {
        ELLE_TRACE("Error writing atime %s: %s", _owner->full_path(), e.what());
      }
#endif
      // FIXME: the only thing that can invalidate _owner is hard links
      // keep tracks of open handle to know if we should refetch
      // or a backend stat call?
      if (!no_fetch && _owner->_handle_count == 1)
      {
        try
        {
          auto address = _owner->_parent->_files.at(_owner->_name).address;
          ELLE_TRACE_SCOPE("fetch first block %x", address);
          _owner->_first_block = elle::cast<MutableBlock>::runtime
            (_owner->_owner.block_store()->fetch(address));
          // access data to detect and report permission issues
          auto len = _owner->_first_block->data().size();
          ELLE_DEBUG("First block has %s bytes", len);
        }
        catch (infinit::model::MissingBlock const& err)
        {
          // This is not a mistake if file is already opened but data has not
          // been pushed yet.
          if (!_owner->_first_block)
          {
            _owner->_handle_count--;
            ELLE_WARN("%s: block missing in model and not in cache", *this);
            throw;
          }
        }
        catch (infinit::model::doughnut::ValidationFailed const& e)
        {
          _owner->_handle_count--;
          ELLE_TRACE("%s: validation failed: %s", *this, e);
          THROW_ACCES;
        }
        catch (elle::Error const& e)
        {
          _owner->_handle_count--;
          ELLE_WARN("%s: unexpected elle exception while fetching: %s",
                    *this, e.what());
          _owner->_first_block.reset();
          throw rfs::Error(EIO, e.what());
        }
        // FIXME: I *really* don't like those.
        catch (std::exception const& e)
        {
          _owner->_handle_count--;
          ELLE_ERR("%s: unexpected exception while fetching: %s",
                   *this, e.what());
          throw rfs::Error(EIO, e.what());
        }
        catch (...)
        {
          _owner->_handle_count--;
          ELLE_ERR("%s: unkown while fetching", *this);
          throw rfs::Error(EIO, "unkown error");
        }
      }
    }

    FileHandle::~FileHandle()
    {
      ELLE_TRACE_SCOPE("%s: close, %s remain", *this, this->_owner->_handle_count-1);
      if (--this->_owner->_handle_count == 0)
      {
        ELLE_TRACE("last handle closed, clear cache");
        //unlink first, so that it can use cached info to delete the blocks
        if (!this->_owner->_parent)
          this->_owner->unlink();
        this->_owner->_blocks.clear();
        this->_owner->_first_block.reset();
        if (this->_owner->_parent)
          this->_owner->_remove_from_cache();
      }
    }

    void
    FileHandle::close()
    {
      if (this->_dirty)
      {
        ELLE_TRACE_SCOPE("%s: flush", *this);
        elle::SafeFinally cleanup([&] {this->_dirty = false;});
        this->_owner->_commit();
      }
      else
        ELLE_DEBUG("%s: skip non-dirty flush", *this);
    }

    int
    FileHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      ELLE_TRACE_SCOPE("%s: read %s at %s", *this, size, offset);
      ELLE_ASSERT_EQ(buffer.size(), size);
      int64_t total_size;
      int32_t block_size;
      if (!_owner->_multi())
      {
        auto& block = _owner->_first_block;
        if (!block)
        {
          ELLE_DEBUG("read on uncached block, fetching");
          auto address = _owner->_parent->_files.at(_owner->_name).address;
          _owner->_first_block = elle::cast<MutableBlock>::runtime
            (_owner->_owner.fetch_or_die(address));
        }
        if (offset + size > block->data().size() - header_size)
        {
          ELLE_DEBUG("read past buffer end, reducing size from %s to %s", size,
                     block->data().size() - offset - header_size);
          size = block->data().size() - offset - header_size;
        }
        memcpy(buffer.mutable_contents(),
               block->data().mutable_contents() + offset + header_size,
               size);
        ELLE_DEBUG("read %s bytes", size);
        return size;
      }
      // multi case
      File::Header h = _owner->_header();
      total_size = h.total_size;
      block_size = h.block_size;
      if (offset >= total_size)
      {
        ELLE_DEBUG("read past end: offset=%s, size=%s", offset, total_size);
        return 0;
      }
      if (signed(offset + size) > total_size)
      {
        ELLE_DEBUG("read past size end, reducing size from %s to %s", size,
                   total_size - offset);
        size = total_size - offset;
      }
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      { // single block case
        off_t block_offset = offset - (off_t)start_block * (off_t)block_size;
        auto const& it = _owner->_blocks.find(start_block);
        AnyBlock* block = nullptr;
        if (it != _owner->_blocks.end())
        {
          ELLE_DEBUG("obtained block %s : %x from cache", start_block, it->second.block.address());
          block = &it->second.block;
          it->second.last_use = std::chrono::system_clock::now();
        }
        else
        {
          block = _owner->_block_at(start_block, false);
          if (block == nullptr)
          { // block would have been allocated: sparse file?
            memset(buffer.mutable_contents(), 0, size);
            ELLE_DEBUG("read %s 0-bytes", size);
            return size;
          }
          ELLE_DEBUG("fetched block %x of size %s", block->address(), block->data().size());
          _owner->check_cache();
        }
        ELLE_ASSERT_LTE(signed(block_offset + size), block_size);
        if (block->data().size() < block_offset + size)
        { // sparse file, eof shrinkage of size was handled above
          long available = block->data().size() - block_offset;
          if (available < 0)
            available = 0;
          ELLE_DEBUG("no data for %s out of %s bytes",
                     size - available, size);
          if (available)
            memcpy(buffer.mutable_contents(),
                   block->data().contents() + block_offset,
                   available);
          memset(buffer.mutable_contents() + available, 0, size - available);
        }
        else
        {
          block->data(
            [&buffer, block_offset, size] (elle::Buffer& data)
            {
              memcpy(buffer.mutable_contents(), &data[block_offset], size);
            });
          ELLE_DEBUG("read %s bytes", size);
        }
        return size;
      }
      else
      { // overlaps two blocks case
        ELLE_ASSERT(start_block == end_block - 1);
        int64_t second_size = (offset + size) % block_size; // second block
        int64_t first_size = size - second_size;
        int64_t second_offset = (int64_t)end_block * (int64_t)block_size;
        ELLE_DEBUG("split %s %s into %s %s and %s %s",
                   size, offset, first_size, offset, second_size, second_offset);
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
      if (size == 0)
        return 0;
      ELLE_TRACE_SCOPE("%s: write %s at %s", *this, size, offset);
      ELLE_ASSERT_EQ(buffer.size(), size);
      this->_dirty = true;
      if (!this->_owner->_multi() &&
          size + offset > this->_owner->default_block_size)
        this->_owner->_switch_to_multi(true);
      if (this->_owner->_multi())
      {
        uint64_t const block_size = this->_owner->_header().block_size;
        int const start_block = offset / block_size;
        int const end_block = (offset + size - 1) / block_size;
        if (start_block == end_block)
          return this->_write_multi_single(
            std::move(buffer), offset, start_block);
        else
          return this->_write_multi_multi(
            std::move(buffer), offset, start_block, end_block);
      }
      else
        return this->_write_single(std::move(buffer), offset);
    }

    int
    FileHandle::_write_single(elle::WeakBuffer buffer, off_t offset)
    {
      auto& block = this->_owner->_first_block;
      if (offset + buffer.size() > block->data().size() - header_size)
      {
        int64_t old_size = block->data().size() - header_size;
        block->data (
          [&] (elle::Buffer& data)
          {
            data.size(offset + buffer.size() + header_size);
            if (old_size < offset)
              memset(data.mutable_contents() + old_size + header_size, 0,
                     offset - old_size);
          });
      }
      block->data(
        [&] (elle::Buffer& data)
        {
          memcpy(data.mutable_contents() + offset + header_size,
                 buffer.contents(), buffer.size());
        });
      return buffer.size();
    }

    int
    FileHandle::_write_multi_single(
      elle::WeakBuffer buffer, off_t offset, int block_idx)
    {
      auto const block_size = this->_owner->_header().block_size;
      auto const size = buffer.size();
      AnyBlock* block;
      auto const it = this->_owner->_blocks.find(block_idx);
      if (it != this->_owner->_blocks.end())
      {
        block = &it->second.block;
        it->second.dirty = true;
        it->second.last_use = std::chrono::system_clock::now();
      }
      else
      {
        block = this->_owner->_block_at(block_idx, true);
        ELLE_ASSERT(block != nullptr);
        this->_owner->check_cache();
        auto const it = this->_owner->_blocks.find(block_idx);
        ELLE_ASSERT(it != this->_owner->_blocks.end());
        it->second.dirty = true;
        it->second.last_use = std::chrono::system_clock::now();
      }
      off_t block_offset = offset % block_size;
      bool growth = false;
      if (block->data().size() < block_offset + size)
      {
        growth = true;
        int64_t old_size = block->data().size();
        block->data(
          [block_offset, size] (elle::Buffer& data)
          {
            data.size(block_offset + size);
          });
        ELLE_DEBUG("grow block from %s to %s",
                   block_offset + size - old_size, block_offset + size);
        if (old_size < block_offset)
          block->zero(old_size, block_offset - old_size);
      }
      block->write(block_offset, buffer.contents(), size);
      if (growth)
      {
        // Check if file size was increased
        File::Header h = this->_owner->_header();
        if (unsigned(h.total_size) < offset + size)
        {
          h.total_size = offset + size;
          ELLE_DEBUG("new file size: %s", h.total_size);
          this->_owner->_header(h);
        }
      }
      return size;
    }

    int
    FileHandle::_write_multi_multi(
      elle::WeakBuffer buffer, off_t offset, int start_block, int end_block)
    {
      uint64_t const block_size = this->_owner->_header().block_size;
      ELLE_ASSERT(start_block == end_block - 1);
      auto const size = buffer.size();
      int64_t second_size = (offset + size) % block_size;
      int64_t first_size = size - second_size;
      int64_t second_offset = (int64_t)end_block * (int64_t)block_size;
      int r1 = this->_write_multi_single(buffer.range(0, first_size),
                                         offset, start_block);
      if (r1 <= 0)
        return r1;
      int r2 = this->_write_multi_single(buffer.range(first_size),
                                         second_offset, end_block);
      if (r2 < 0)
        return r2;
      // Assuming linear writes, this is a good time to flush start block since
      // it just got filled.
      File::CacheEntry& ent = this->_owner->_blocks.at(start_block);
      Address prev = ent.block.address();
      Address cur = ent.block.store(*this->_owner->_owner.block_store(),
        ent.new_block? model::STORE_INSERT : model::STORE_ANY);
      if (cur != prev)
      {
        ELLE_DEBUG("Changing address of block %s: %s -> %s", start_block,
                   prev, cur);
        int offset = (start_block+1) * sizeof(Address);
        this->_owner->_first_block->data([&](elle::Buffer& data)
          {
            if (data.size() < offset + sizeof(Address::Value))
              data.size(offset + sizeof(Address::Value));
            memcpy(data.mutable_contents() + offset, cur.value(),
                   sizeof(Address::Value));
          });
        if (!ent.new_block)
          this->_owner->_owner.block_store()->remove(prev);
      }
      ent.dirty = false;
      ent.new_block = false;
      this->_owner->_owner.store_or_die(*this->_owner->_first_block,
                                        this->_owner->_first_block_new
                                        ? model::STORE_INSERT
                                        : model::STORE_ANY);
      this->_owner->_first_block_new = false;
      return r1 + r2;
    }

    void
    FileHandle::ftruncate(off_t offset)
    {
      return _owner->truncate(offset);
    }

    void
    FileHandle::fsyncdir(int datasync)
    {
      ELLE_TRACE("%s: fsyncdir", *this);
      fsync(datasync);
    }

    void
    FileHandle::fsync(int datasync)
    {
      ELLE_LOG("%s: fsync", *this);
    }

    void
    FileHandle::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "FileHandle(%x, \"%s\")",
                    this, this->_owner->_name);
    }
  }
}
