#include <memo/filesystem/FileHandle.hh>

#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/min_element.hpp>

#include <elle/algorithm.hh>
#include <elle/cast.hh>
#include <elle/cryptography/SecretKey.hh>
#include <elle/cryptography/random.hh>
#include <elle/serialization/binary.hh>

#include <memo/environ.hh>
#include <memo/filesystem/Directory.hh>
#include <memo/model/MissingBlock.hh>
#include <memo/model/doughnut/Doughnut.hh>

ELLE_LOG_COMPONENT("memo.filesystem.FileHandle");

namespace
{
  auto const max_embed_size = memo::getenv("MAX_EMBED_SIZE", 8192);
  auto const lookahead_blocks = memo::getenv("LOOKAHEAD_BLOCKS", 5);
  auto const max_lookahead_threads = memo::getenv("LOOKAHEAD_THREADS", 3);
  using Size = elle::Buffer::Size;
  auto const default_first_block_size = Size(memo::getenv("FIRST_BLOCK_DATA_SIZE", 0));
}

namespace memo
{
  namespace filesystem
  {
    namespace
    {
      std::chrono::high_resolution_clock::time_point
      now()
      {
        return std::chrono::high_resolution_clock::now();
      }
    }

    FileHandle::FileHandle(FileSystem& owner,
                           FileData data,
                           bool dirty)
      : _owner(owner)
    {
      auto it = owner.file_buffers().find(data.address());
      if (it != owner.file_buffers().end())
        this->_buffer = it->second.lock();
      if (!this->_buffer)
      {
        this->_buffer = std::make_shared<FileBuffer>(owner, data, dirty);
        owner.file_buffers().insert(std::make_pair(data.address(), this->_buffer));
      }
      else
      {
        if (dirty)
          this->_buffer->_dirty = true;
      }
    }

    FileBuffer::FileBuffer(FileSystem& fs,
                           FileData data,
                           bool dirty)
      : _dirty(dirty)
      , _fs(fs)
      , _file(std::move(data))
    {}

    FileHandle::~FileHandle()
    {
      try
      {
        auto addr = this->_buffer->_file.address();
        if (!this->_close_failure)
          this->_buffer->close(this);
        std::weak_ptr<FileBuffer> fb = this->_buffer;
        this->_buffer.reset();
        if (!fb.lock())
          this->_owner.file_buffers().erase(addr);
      }
      catch (elle::Error const& e)
      {
       ELLE_ERR("fatal error: exception escaping ~FileHandle: %s\n%s",
                elle::exception_string(), e.backtrace());
       throw;
      }
    }

    FileBuffer::~FileBuffer()
    {
      while (_prefetchers_count)
        elle::reactor::sleep(20_ms);
      if (this->_remove_data)
      {
        // FIXME optimize pass removal data
        for (unsigned i = 0; i < this->_file._fat.size(); ++i)
        {
          ELLE_DEBUG_SCOPE("removing %s: %f", i, this->_file._fat[i].first);
          unchecked_remove_chb(*this->_fs.block_store(), this->_file._fat[i].first, this->_file.address());
        }
        ELLE_DEBUG_SCOPE("removing first block at %f", this->_file.address());
        unchecked_remove(*this->_fs.block_store(), this->_file.address());
      }
    }

    void
    FileHandle::close()
    {
      this->_close_failure = true;
      this->_buffer->close(this);
      this->_close_failure = false;
    }

    void
    FileBuffer::close(FileHandle* src)
    {
      if (this->_dirty)
      {
        ELLE_TRACE_SCOPE("%s: flush", *this);
        _commit_all(src);
      }
      else
        ELLE_DEBUG("%s: skip non-dirty flush", *this);
    }

    int
    FileHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      return _buffer->read(this, buffer, size, offset);
    }

    int FileBuffer::read(FileHandle* src, elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      ELLE_TRACE_SCOPE("%s: read %s at %s", *this, size, offset);
      ELLE_TRACE("%s: have %s bytes and %s fat entries totalling %s", *this,
        _file._data.size(), _file._fat.size(), _file._header.size);
      ELLE_ASSERT_EQ(buffer.size(), size);
      if (offset < signed(_file._data.size()))
      {
        size_t size1 = std::min(size, size_t(_file._data.size() - offset));
        memcpy(buffer.mutable_contents(),
               _file._data.contents() + offset,
               size1);
        if (size1 == size || _file._fat.empty())
          return size1;
        return size1 + read(
          src,
          elle::WeakBuffer(buffer.mutable_contents() + size1, size - size1),
          size - size1, offset + size1);
      }
      // multi case
      int64_t total_size = _file._header.size;
      int32_t block_size = _file._header.block_size;
      if (offset >= total_size)
      {
        ELLE_DEBUG("read past end: offset=%s, size=%s", offset, total_size);
        return 0;
      }
      ELLE_DEBUG("past eof check: o=%s, ts=%s, required=%s",
                 offset, total_size, size);
      if (signed(offset + size) > total_size)
      {
        ELLE_DEBUG("read past size end, reducing size from %s to %s", size,
            total_size - offset);
        size = total_size - offset;
      }
      // scroll offset so that offset 0 is first fat block
      offset -= _file._data.size();
      auto end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      _last_read_block = start_block;
      _check_prefetch();
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      {
        // single block case
        auto block_offset = offset - (off_t)start_block * (off_t)block_size;
        auto it = _blocks.find(start_block);
        std::shared_ptr<elle::Buffer> block;
        if (it != _blocks.end())
        {
          ELLE_DEBUG("obtained block %s from cache", start_block);
          elle::reactor::wait(it->second.ready);
          if (it->second.block)
          {
            block = it->second.block;
            it->second.last_use = now();
          }
          else
          {
            ELLE_WARN("read failure on block %s", start_block);
            _blocks.erase(start_block);
          }
        }
        if (!block)
        {
          block = _block_at(start_block, false);
          if (!block)
          {
            // block would have been allocated: sparse file?
            memset(buffer.mutable_contents(), 0, size);
            ELLE_LOG("read %s 0-bytes, missing block %s", size, start_block);
            return size;
          }
          ELLE_DEBUG("fetched block of size %s", block->size());
          check_cache(src);
        }
        ELLE_ASSERT_LTE(signed(block_offset + size), block_size);
        if (signed(block->size()) < signed(block_offset + size))
        {
          // sparse file, eof shrinkage of size was handled above
          long available = block->size() - block_offset;
          if (available < 0)
            available = 0;
          ELLE_DEBUG("no data for %s out of %s bytes",
              size - available, size);
          if (available)
            memcpy(buffer.mutable_contents(),
                   block->contents() + block_offset,
                   available);
          memset(buffer.mutable_contents() + available, 0, size - available);
        }
        else
        {
          memcpy(buffer.mutable_contents(), &(*block)[block_offset], size);
          ELLE_DEBUG("read %s bytes", size);
        }
        return size;
      }
      else
      {
        // overlaps two blocks case
        ELLE_ASSERT(start_block == end_block - 1);
        int64_t second_size = (offset + size) % block_size; // second block
        int64_t first_size = size - second_size;
        int64_t second_offset = (int64_t)end_block * (int64_t)block_size;
        ELLE_DEBUG("split %s %s into %s %s and %s %s",
            size, offset, first_size, offset, second_size, second_offset);
        int r1 = read(src, elle::WeakBuffer(buffer.mutable_contents(), first_size),
                      first_size, offset + _file._data.size());
        if (r1 <= 0)
          return r1;
        int r2 = read(src, elle::WeakBuffer(buffer.mutable_contents() + first_size, second_size),
                      second_size, second_offset + _file._data.size());
        if (r2 < 0)
          return r2;
        ELLE_DEBUG("read %s+%s=%s bytes", r1, r2, r1+r2);
        return r1 + r2;
      }
    }

    int
    FileHandle::write(elle::ConstWeakBuffer buffer,
                      size_t size,
                      off_t offset)
    {
      return _buffer->write(this, buffer, size, offset);
    }

    int
    FileBuffer::write(FileHandle* src,
                      elle::ConstWeakBuffer buffer,
                      size_t size,
                      off_t offset)
    {
      if (size == 0)
        return 0;
      // figure out first block size for this file
      auto max_first_block_size =
        this->_file._fat.empty()
        ? std::max(default_first_block_size, _file._data.size())
        : _file._data.size();
      ELLE_TRACE_SCOPE("%s: write %s bytes at offset %s fbs %s)",
                       *this, size, offset, max_first_block_size);
      ELLE_ASSERT_EQ(buffer.size(), size);
      this->_dirty = true;
      _file._header.mtime = time(nullptr);
      if (offset < signed(max_first_block_size))
      {
        // write on first block
        _fat_changed = true;
        auto wend = std::min(Size(size + offset), max_first_block_size);
        if (_file._data.size() < wend)
        {
          auto oldsz = _file._data.size();
          _file._data.size(wend);
          memset(_file._data.mutable_contents() + oldsz,
                 0,
                 wend - oldsz);
        }
        auto to_write = wend - offset;
        memcpy(_file._data.mutable_contents() + offset,
               buffer.contents(),
               to_write);
        this->_file._header.size = std::max(this->_file._header.size,
                                              uint64_t(offset + size));
        return to_write + write(
          src,
          elle::ConstWeakBuffer(buffer.contents() + to_write, size - to_write),
          size - to_write, offset + to_write);
      }
      else
      {
        this->_file._header.size = std::max(this->_file._header.size,
                                              uint64_t(offset + size));
        // In case we skipped embeded first block, fill it
        this->_file._data.size(max_first_block_size);
        offset -= max_first_block_size;
        uint64_t const block_size = this->_file._header.block_size;
        int const start_block = offset / block_size;
        int const end_block = (offset + size - 1) / block_size;
        if (start_block == end_block)
          return this->_write_multi_single(
              src, std::move(buffer), offset, start_block);
        else
          return this->_write_multi_multi(
              src, std::move(buffer), offset, start_block, end_block);
      }
    }

    int
    FileBuffer::_write_multi_single(FileHandle* src,
                                    elle::ConstWeakBuffer buffer,
                                    off_t offset,
                                    int block_idx)
    {
      auto const block_size = this->_file._header.block_size;
      auto const size = buffer.size();
      std::shared_ptr<elle::Buffer> block;
      auto const it = _blocks.find(block_idx);
      if (it != _blocks.end())
      {
        elle::reactor::wait(it->second.ready);
        block = it->second.block;
        it->second.dirty = true;
        it->second.last_use = now();
        it->second.writers.insert(src);
      }
      else
      {
        block = _block_at(block_idx, true);
        ELLE_ASSERT(block);
        check_cache(src);
        auto it = _blocks.find(block_idx);
        if (it == _blocks.end())
        {
          _block_at(block_idx, true);
          it = _blocks.find(block_idx);
        }
        ELLE_ASSERT(it != _blocks.end());
        elle::reactor::wait(it->second.ready);
        it->second.dirty = true;
        it->second.last_use = now();
        it->second.writers.insert(src);
      }
      off_t block_offset = offset % block_size;
      if (block->size() < block_offset + size)
      {
        int64_t old_size = block->size();
        block->size(block_offset + size);
        ELLE_DEBUG("grow block from %s to %s",
            block_offset + size - old_size, block_offset + size);
        if (old_size < block_offset)
          memset(block->mutable_contents() + old_size,
                 0,
                 block_offset - old_size);
      }
      memcpy(block->mutable_contents() + block_offset, buffer.contents(), size);
      return size;
    }

    int
    FileBuffer::_write_multi_multi(FileHandle* src,
                                   elle::ConstWeakBuffer buffer,
                                   off_t offset,
                                   int start_block,
                                   int end_block)
    {
      uint64_t const block_size = this->_file._header.block_size;
      ELLE_ASSERT(start_block == end_block - 1);
      auto const size = buffer.size();
      int64_t second_size = (offset + size) % block_size;
      int64_t first_size = size - second_size;
      int64_t second_offset = (int64_t)end_block * (int64_t)block_size;
      int r1 = this->_write_multi_single(src, buffer.range(0, first_size),
          offset, start_block);
      if (r1 <= 0)
        return r1;
      int r2 = this->_write_multi_single(src, buffer.range(first_size),
          second_offset, end_block);
      if (r2 < 0)
        return r2;
      return r1 + r2;
    }

    void
    FileHandle::ftruncate(off_t new_size)
    {
      return _buffer->ftruncate(this, new_size);
    }

    void
    FileBuffer::ftruncate(FileHandle* src, off_t new_size)
    {
      off_t current = _file._header.size;
      ELLE_DEBUG_SCOPE("%s: ftruncate %s -> %s", this, current, new_size);
      if (new_size == signed(current))
        return;
      if (new_size > signed(current))
      {
        this->_file._header.size = new_size;
        _dirty = true;
        _fat_changed = true;
        return;
      }
      uint64_t first_block_size = _file._data.size();
      for (int i = this->_file._fat.size() - 1; i >= 0; --i)
      {
        auto offset = first_block_size + i * _file._header.block_size;
        if (signed(offset) >= new_size)
        // Kick the block
        {
          ELLE_DEBUG("removing from fat at %s", i);
          unchecked_remove_chb(*this->_fs.block_store(), _file._fat[i].first, _file._address);
          _file._fat.pop_back();
          _blocks.erase(i);
        }
        else if (signed(offset + _file._header.block_size) >= new_size)
        // Truncate the block
        {
          auto targetsize = new_size - offset;
          auto it = _blocks.find(i);
          if (it != _blocks.end())
          {
            if (!it->second.block)
              elle::reactor::wait(it->second.ready);
            if (auto data = it->second.block)
            {
              data->size(targetsize);
              it->second.dirty = true;
            }
          }
          else
          {
            auto buf = _block_at(i, true);
            if (buf->size() != targetsize)
            {
              buf->size(targetsize);
            }
            _blocks.at(i).dirty = true;
          }
        }
      }
      if (new_size < signed(_file._data.size()))
        _file._data.size(new_size);
      this->_file._header.size = new_size;
      _fat_changed = true;
      _dirty = true;
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
      ELLE_TRACE_SCOPE("%s: fsync", *this);
      _buffer->_commit_all(this);
    }

    void
    FileHandle::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "FileHandle(%x, \"%s\")",
                    (void*)(this), this->_buffer->_file._address);
    }

    std::shared_ptr<elle::Buffer>
    FileBuffer::_block_at(int index, bool create)
    {
      ELLE_ASSERT_GTE(index, 0);
      auto it = this->_blocks.find(index);
      if (it != this->_blocks.end())
      {
        elle::reactor::wait(it->second.ready);
        it->second.last_use = now();
        return it->second.block;
      }
      if (_file._fat.size() <= unsigned(index))
      {
        ELLE_TRACE("%s: block_at(%s) out of range", *this, index);
        if (!create)
          return nullptr;
        _file._fat.resize(index+1, FileData::FatEntry(Address::null, {}));
      }
      auto p = this->_blocks.emplace(index, CacheEntry{});
      assert(p.second);
      auto& c = p.first->second;
      if (_file._fat[index].first == Address::null)
      {
        c.block = std::make_shared<elle::Buffer>();
        c.ready.open();
      }
      else
      {
        c.ready.close();
        auto const addr = Address(this->_file._fat[index].first.value(),
                                  model::flags::immutable_block, false);
        auto const secret = _file._fat[index].second;
        ELLE_TRACE("Fetching %s at %f", index, addr);
        elle::SafeFinally open_ready([&] {
            c.ready.open();
        });
        auto block = fetch_or_die(*_fs.block_store(), addr, {},
                                  this->_file.path() / elle::sprintf("<%f>", addr));
        auto crypted = block->take_data();
        if (secret.empty())
          c.block = std::make_shared<elle::Buffer>(std::move(crypted));
        else
        {
          auto const sk = elle::cryptography::SecretKey(secret);
          c.block = std::make_shared<elle::Buffer>(sk.decipher(crypted));
        }
      }
      c.last_use = now();
      c.dirty = false; // we just fetched or inserted it
      return c.block;
    }

    void
    FileBuffer::_check_prefetch()
    {
      // Check if we need to relaunch a prefetcher.
      for (int nidx = _last_read_block + 1;
           nidx < _last_read_block + lookahead_blocks
             && nidx < signed(_file._fat.size())
             && _prefetchers_count < max_lookahead_threads;
           ++nidx)
        if (_file._fat[nidx].first != Address::null
            && !elle::contains(this->_blocks, nidx))
        {
          _prefetch(nidx);
          break;
        }
    }

    void
    FileBuffer::_prefetch(int idx)
    {
      ELLE_TRACE("%s: prefetch index %s", *this, idx);
      auto p = this->_blocks.emplace(idx, CacheEntry{});
      auto& c = p.first->second;
      c.last_use = now();
      c.dirty = false;
      auto const addr = Address(this->_file._fat[idx].first.value(),
                                model::flags::immutable_block, false);
      auto const key = _file._fat[idx].second;
      ++_prefetchers_count;
      new elle::reactor::Thread("prefetcher", [this, addr, idx, key] {
          std::unique_ptr<model::blocks::Block> bl;
          try
          {
            bl = fetch_or_die(*_fs.block_store(), addr, {},
                              this->_file.path() / elle::sprintf("<%f>", addr));
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("Prefetcher error fetching %x: %s", addr, e);
            --this->_prefetchers_count;
            this->_blocks[idx].ready.open();
            return;
          }
          ELLE_TRACE("Prefetcher inserting value at %s", idx);
          auto crypted = bl->take_data();
          std::shared_ptr<elle::Buffer> b;
          if (key.empty())
            b = std::make_shared<elle::Buffer>(std::move(crypted));
          else
            b = std::make_shared<elle::Buffer>(
              elle::cryptography::SecretKey(key).decipher(crypted));
          this->_blocks[idx].last_use = now();
          this->_blocks[idx].block = b;
          this->_blocks[idx].ready.open();
          --this->_prefetchers_count;
          this->_check_prefetch();
          this->check_cache(nullptr, this->max_cache_size);
      }, true);
    }

    void
    FileBuffer::_commit_first(FileHandle* src)
    {
      _file.write(_fs, WriteTarget::data | WriteTarget::times,
                  DirectoryData::null_block, _first_block_new);
      _first_block_new = false;
    }

    void
    FileBuffer::_commit_all(FileHandle* src)
    {
      ELLE_TRACE_SCOPE("%s: commit all", this);
      check_cache(src, 0);
    }

    struct InsertBlockResolver
      : public model::DummyConflictResolver
    {
      using Super = memo::model::DummyConflictResolver;
      InsertBlockResolver(boost::filesystem::path const& path,
                          Address const& address,
                          bool async = false)
        : Super()
        , _path(path.string())
        , _address(address)
        , _async(async)
      {}

      InsertBlockResolver(elle::serialization::Serializer& s,
          elle::Version const& version)
        : Super()
      {
        this->serialize(s, version);
      }

      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override
      {
        Super::serialize(s, version);
        s.serialize("path", this->_path);
        s.serialize("address", this->_address);
        s.serialize("async", this->_async);
      }

      std::string
      description() const override
      {
        return elle::sprintf("insert block (%f) for %s (async: %s)",
                             this->_address, this->_path, this->_async);
      }

      ELLE_ATTRIBUTE(std::string, path);
      ELLE_ATTRIBUTE(Address, address);
      ELLE_ATTRIBUTE(bool, async)
    };

    static const elle::serialization::Hierarchy<memo::model::ConflictResolver>::
    Register<InsertBlockResolver> _register_insert_block_resolver("insert_block_resolver");

    std::function<void ()>
    FileBuffer::_flush_block(int id, CacheEntry& entry)
    {
      if (entry.dirty)
        return [this, id, data_ = elle::Buffer(*entry.block)] () mutable
        {
          auto ent = [this, id]() -> CacheEntry*
            {
              auto it = this->_blocks.find(id);
              if (it != this->_blocks.end())
              {
                auto res = &it->second;
                // FIXME: is this safe?
                elle::reactor::wait(res->ready);
                return res;
              }
              else
                return nullptr;
            }();
          if (ent)
            ent->ready.close();
          elle::SafeFinally interrupt_guard([&] {
              ELLE_WARN("Flusher %s was interrupted", id);
              if (ent)
                ent->ready.open();
          });
          bool encrypt = dynamic_cast<model::doughnut::Doughnut const&>(*this->_fs.block_store())
            .encrypt_options().encrypt_at_rest;
          std::string key;
          elle::Buffer cdata;
          if (encrypt)
          {
            key = elle::cryptography::random::generate<elle::Buffer>(32).string();
            if (data_.size() >= 262144)
              elle::reactor::background([&] {
                cdata = elle::cryptography::SecretKey(key).encipher(data_);
              });
            else
              cdata = elle::cryptography::SecretKey(key).encipher(data_);
          }
          else
            cdata = elle::Buffer(data_.contents(), data_.size());
          auto block = this->_fs.block_store()->make_block<ImmutableBlock>(
            std::move(cdata), this->_file._address);
          auto baddr = block->address();
          this->_fs.block_store()->insert(
            std::move(block),
            std::make_unique<InsertBlockResolver>(this->_file.path(), baddr));
          auto prev = Address::null;
          if (signed(this->_file._fat.size()) > id)
            prev = _file._fat.at(id).first;
          this->_file._fat[id] = FileData::FatEntry(baddr, key);
          this->_fat_changed = true;
          if (prev != Address::null)
            unchecked_remove(*this->_fs.block_store(), prev);
          if (ent)
            ent->ready.open();
          interrupt_guard.abort();
        };
      else
        return {};
    }

    bool
    FileBuffer::check_cache(FileHandle* src, int cache_size)
    {
      if (cache_size < 0)
        cache_size = max_cache_size;
      if (cache_size == 0)
      {
        // Final flush, wait on all async ops concerning src
        while (true)
        {
          auto it = boost::find_if(_flushers,
                                   [&](Flusher const& f) {
                                     return elle::contains(f.second, src);
                                   });
          if (it == _flushers.end())
            break;
          it->second.erase(src);
          auto thread = it->first.get();
          elle::reactor::wait(*it->first);
          it = boost::find_if(_flushers,
                              [&](Flusher const& f) {
                                return f.first.get() == thread;
                              });
          if (it != _flushers.end() && it->second.empty())
            _flushers.erase(it);
        }
      }
      else
      {
        // Just wait on finished ops to get exceptions
        for (int i=0; i<signed(_flushers.size()); ++i)
        {
          if (_flushers[i].first->done()
              && contains(_flushers[i].second, src))
          {
            elle::reactor::wait(*_flushers[i].first); // will not yield
            _flushers[i].second.erase(src);
            if (_flushers[i].second.empty())
            {
              std::swap(_flushers[i], _flushers[_flushers.size()-1]);
              _flushers.pop_back();
              --i;
            }
          }
        }
      }
      // optimize by embeding data in ACB for small payloads
      if (cache_size == 0 && max_embed_size && !default_first_block_size
          && this->_blocks.size() == 1
          && this->_blocks.begin()->first == 0
          && this->_blocks.at(0).dirty
          && this->_file._fat.size() == 1
          && this->_file._fat[0].first == Address::null
          && signed(this->_blocks.at(0).block->size()
                    + this->_file._data.size()) <= max_embed_size)
      {
        ELLE_TRACE_SCOPE("%s: enabling data embed", this);
        this->_file._data.append(this->_blocks.at(0).block->contents(),
                                 this->_blocks.at(0).block->size());
        this->_file._fat.clear();
        this->_blocks.clear();
        this->_commit_first(src);
        _first_block_new = false;
        _fat_changed = false;
        return true;
      }
      if (cache_size == 0)
      {
        // flush all blocks src wrote to
        auto flushers = std::vector<std::function<void ()>>{};
        for (auto& b: this->_blocks)
        {
          if (b.second.dirty && contains(b.second.writers, src))
          {
            flushers.emplace_back(this->_flush_block(b.first, b.second));
            b.second.dirty = false;
            b.second.writers.clear();
          }
        }
        for (auto& f: flushers)
          f();
      }
      else
      {
        while (this->_blocks.size() > unsigned(cache_size))
        {
          auto it = boost::min_element(this->_blocks,
            [](auto const& a, auto const& b)
            {
              if (a.second.ready.opened() != b.second.ready.opened())
                return a.second.ready.opened();
              else
                return (std::tie(a.second.last_use, a.first)
                        < std::tie(b.second.last_use, b.first));
            });
          ELLE_TRACE("Removing block %s from cache", it->first);
          {
            if (!it->second.ready.opened())
            {
              ELLE_DEBUG("Waiting for readyness");
              elle::reactor::wait(it->second.ready);
            }
            auto entry = std::move(*it);
            auto writers = entry.second.writers;
            this->_blocks.erase(it);
            if (auto f = this->_flush_block(entry.first, entry.second))
              if (cache_size == 0)
                f();
              else
                this->_flushers.emplace_back(
                  new elle::reactor::Thread("flusher",
                                      [f] { f(); },
                                      elle::reactor::managed = true),
                  writers);
          }
        }
      }
      bool prev = this->_fat_changed;
      if (this->_fat_changed)
      {
        ELLE_DEBUG_SCOPE("FAT with %s entries changed, commit first block",
                         this->_file._fat.size());
        this->_commit_first(src);
        _first_block_new = false;
        _fat_changed = false;
      }
      return prev;
    }
  }
}
