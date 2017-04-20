#pragma once

#include <infinit/filesystem/umbrella.hh>
#include <infinit/filesystem/File.hh>

namespace infinit
{
  namespace filesystem
  {
    class FileBuffer;

    class FileHandle
      : public rfs::Handle
      , public elle::Printable
    {
    public:
      FileHandle(FileSystem& owner,
                 FileData data,
                 bool mark_dirty = false);
      ~FileHandle();
      int
      read(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      int
      write(elle::ConstWeakBuffer buffer, size_t size, off_t offset) override;
      void
      ftruncate(off_t offset) override;
      void
      fsync(int datasync) override;
      void
      fsyncdir(int datasync) override;
      void
      close() override;
      void
      print(std::ostream& stream) const override;
    private:
      std::shared_ptr<FileBuffer> _buffer;
      FileSystem& _owner;
      bool _close_failure = false;
    };

    class FileBuffer
    {
    public:
      FileBuffer(FileSystem& fs, FileData data, bool dirty);
      ~FileBuffer();
      int read(FileHandle* src, elle::WeakBuffer buffer, size_t size, off_t offset);
      int write(FileHandle* src, elle::ConstWeakBuffer buffer, size_t size, off_t offset);
      void ftruncate(FileHandle* src, off_t offset);
      void close(FileHandle* src);
    private:
      int
      _write_single(elle::ConstWeakBuffer buffer, off_t offset);
      int
      _write_multi_single(FileHandle* src, elle::ConstWeakBuffer buffer, off_t offset, int block);
      int
      _write_multi_multi(FileHandle* src, elle::ConstWeakBuffer buffer, off_t offset,
                         int start_block, int end_block);
      ELLE_ATTRIBUTE(bool, dirty);

      struct CacheEntry
      {
        CacheEntry() = default;
        std::shared_ptr<elle::Buffer> block;
        bool dirty = false;
        std::chrono::high_resolution_clock::time_point last_use;
        elle::reactor::Barrier ready;
        std::unordered_set<FileHandle*> writers;
      };

      void _commit_first(FileHandle* src);
      void _commit_all(FileHandle* src);
      std::function<void ()>
      _flush_block(int id, CacheEntry& entry);
      void _prefetch(int idx);
      void _check_prefetch();
      // check cached data size, remove entries if needed
      bool check_cache(FileHandle* src, int cache_size = -1);
      /* Get address for given block index.
      * @param create: if true, allow creation of a new block as needed
      *                else returns nullptr if creation was required
      */
      std::shared_ptr<elle::Buffer> _block_at(int index, bool create);
      FileSystem& _fs;
      FileData _file;
      using Flusher = std::pair<elle::reactor::Thread::unique_ptr,
                                std::unordered_set<FileHandle*>>;
      std::vector<Flusher> _flushers;
      std::unordered_map<int, CacheEntry> _blocks;
      bool _first_block_new = false;
      bool _fat_changed = false;
      int _prefetchers_count = 0; // number of running prefetchers
      int _last_read_block = 0; // block hit by last read operation
      bool _remove_data = false; // there are no more links, remove data.
      static const uint64_t default_first_block_size;
      static const unsigned long max_cache_size = 20; // in blocks
      friend class File;
      friend class FileHandle;
    };
  }
}
