#ifndef INFINIT_FILESYSTEM_FILEHANDLE_HH
# define INFINIT_FILESYSTEM_FILEHANDLE_HH

# include <infinit/filesystem/umbrella.hh>
# include <infinit/filesystem/File.hh>

namespace infinit
{
  namespace filesystem
  {
    class FileHandle
      : public rfs::Handle
      , public elle::Printable
    {
    public:
      FileHandle(model::Model& model,
                 FileData data,
                 bool writable,
                 bool update_folder_mtime=false,
                 bool no_prefetch = false,
                 bool mark_dirty = false);
      ~FileHandle();
      virtual
      int
      read(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      virtual
      int
      write(elle::ConstWeakBuffer buffer, size_t size, off_t offset) override;
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
      _write_single(elle::ConstWeakBuffer buffer, off_t offset);
      int
      _write_multi_single(elle::ConstWeakBuffer buffer, off_t offset, int block);
      int
      _write_multi_multi(elle::ConstWeakBuffer buffer, off_t offset,
                         int start_block, int end_block);
      ELLE_ATTRIBUTE(bool, dirty);
      ELLE_ATTRIBUTE(bool, writable);
      struct CacheEntry
      {
        std::shared_ptr<elle::Buffer> block;
        bool dirty;
        std::chrono::system_clock::time_point last_use;
        reactor::Barrier ready;
      };
      void _commit_first();
      void _commit_all();
      bool _flush_block(int id);
      void _prefetch(int idx);
      void _check_prefetch();
      // check cached data size, remove entries if needed
      bool check_cache(int cache_size = -1);
      /* Get address for given block index.
      * @param create: if true, allow creation of a new block as needed
      *                else returns nullptr if creation was required
      */
      std::shared_ptr<elle::Buffer> _block_at(int index, bool create);
      model::Model& _model;
      FileData _file;
      std::vector<reactor::Thread::unique_ptr> _flushers;
      std::unordered_map<int, CacheEntry> _blocks;
      bool _first_block_new;
      bool _fat_changed;
      int _prefetchers_count; // number of running prefetchers
      int _last_read_block; // block hit by last read operation
      static const uint64_t default_first_block_size;
      static const unsigned long max_cache_size = 20; // in blocks    };
    };
  }
}

#endif
