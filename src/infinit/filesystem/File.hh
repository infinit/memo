#ifndef INFINIT_FILESYSTEM_FILE_HH
# define INFINIT_FILESYSTEM_FILE_HH

# include <reactor/filesystem.hh>
# include <infinit/filesystem/Node.hh>
# include <infinit/filesystem/AnyBlock.hh>
# include <infinit/filesystem/umbrella.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = reactor::filesystem;

    class Directory;
    typedef std::shared_ptr<Directory> DirectoryPtr;
    typedef infinit::model::blocks::MutableBlock MutableBlock;

    class File
      : public rfs::Path
      , public Node
    {
      public:
        File(DirectoryPtr parent, FileSystem& owner, std::string const& name,
            std::unique_ptr<MutableBlock> b = std::unique_ptr<MutableBlock>());
        ~File();
        void stat(struct stat*) override;
        void list_directory(rfs::OnDirectoryEntry cb) override THROW_NOTDIR;
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
        void utimens(const struct timespec tv[2]) override;
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
        static const unsigned long default_block_size = 1024 * 1024;
        static const unsigned long max_cache_size = 20; // in blocks
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
      int _r_handle_count;
      int _rw_handle_count;
      boost::filesystem::path _full_path;
      std::unique_ptr<model::blocks::MutableBlock::Cache> _block_cache;
    };
  }
}

#endif
