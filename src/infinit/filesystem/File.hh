#ifndef INFINIT_FILESYSTEM_FILE_HH
# define INFINIT_FILESYSTEM_FILE_HH

# include <reactor/filesystem.hh>
# include <reactor/Barrier.hh>
# include <reactor/thread.hh>
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
        std::shared_ptr<Path> child(std::string const& name) override;
        bool allow_cache() override;
        // check cached data size, remove entries if needed
        bool check_cache(int cache_size = -1);
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

        /* Get address for given block index.
         * @param create: if true, allow creation of a new block as needed
         *                else returns nullptr if creation was required
         */
      std::shared_ptr<AnyBlock> _block_at(int index, bool create);

      void _ensure_first_block();
      void _fetch() override;
      void _commit() override;
      model::blocks::ACLBlock* _header_block() override;
      void _commit_first(bool final_flush);
      void _commit_all();
      bool _flush_block(int id);
      void _prefetch(int idx);
      void _check_prefetch();
      struct CacheEntry
      {
        std::shared_ptr<AnyBlock> block;
        bool dirty;
        std::chrono::system_clock::time_point last_use;
        bool new_block;
        reactor::Barrier ready;
      };

      std::vector<reactor::Thread::unique_ptr> _flushers;
      std::unordered_map<int, CacheEntry> _blocks;
      std::unique_ptr<MutableBlock> _first_block;
      bool _first_block_new;
      int _r_handle_count;
      int _rw_handle_count;
      bool _fat_changed;
      boost::filesystem::path _full_path;
      typedef std::pair<Address, std::string> FatEntry; // (address, key)
      ELLE_ATTRIBUTE_R(std::vector<FatEntry>, fat);
      elle::Buffer _data; // first block data
      int _prefetchers_count; // number of running prefetchers
      int _last_read_block; // block hit by last read operation
      static const uint64_t first_block_size;
    };
  }
}

#endif
