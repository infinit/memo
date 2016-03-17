#include <infinit/filesystem/FileHandle.hh>

#include <cryptography/SecretKey.hh>
#include <cryptography/random.hh>

#include <infinit/filesystem/Directory.hh>
#include <elle/cast.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/binary.hh>
#include <infinit/model/doughnut/Doughnut.hh>

#include <infinit/model/MissingBlock.hh>

ELLE_LOG_COMPONENT("infinit.filesystem.FileHandle");

namespace infinit
{
  namespace filesystem
  {
    const uint64_t FileHandle::default_first_block_size = 16384;
    static const int lookahead_blocks =
      std::stoi(elle::os::getenv("INFINIT_LOOKAHEAD_BLOCKS", "5"));
    static const int max_lookahead_threads =
      std::stoi(elle::os::getenv("INFINIT_LOOKAHEAD_THREADS", "3"));

    FileHandle::FileHandle(model::Model& model,
                           FileData data,
                           bool writable,
                           bool push_mtime,
                           bool no_fetch,
                           bool dirty)
      : _dirty(dirty)
      , _writable(writable)
      , _model(model)
      , _file(data)
      , _first_block_new(false)
      , _fat_changed(false)
      , _prefetchers_count(0)
      , _last_read_block(0)
    {
      if (_writable)
      {
        auto it = File::_size_map.insert(std::make_pair(data.address(),
          std::make_pair(data.header().size, 0))).first;
        it->second.second++;
        it->second.first = std::max(it->second.first, data.header().size);
      }
    }

    FileHandle::~FileHandle()
    {
      if (_writable)
      {
        auto it = File::_size_map.find(_file.address());
        if (!--it->second.second)
          File::_size_map.erase(it);
      }

      while (_prefetchers_count)
        reactor::sleep(20_ms);
    }

    void
    FileHandle::close()
    {
      if (this->_dirty)
      {
        ELLE_TRACE_SCOPE("%s: flush", *this);
        elle::SafeFinally cleanup([&] {this->_dirty = false;});
        _commit_all();
      }
      else
        ELLE_DEBUG("%s: skip non-dirty flush", *this);
    }

    int
    FileHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      ELLE_TRACE_SCOPE("%s: read %s at %s", *this, size, offset);
      ELLE_TRACE("%s: have %s bytes and %s fat entries totalling %s", *this,
        _file._data.size(), _file._fat.size(), _file._header.size);
      ELLE_ASSERT_EQ(buffer.size(), size);
      int64_t total_size;
      int32_t block_size;
      if (offset < signed(_file._data.size()))
      {
        size_t size1 = std::min(size, size_t(_file._data.size() - offset));
        memcpy(buffer.mutable_contents(),
               _file._data.contents() + offset,
               size1);
        if (size1 == size || _file._fat.empty())
          return size1;
        return size1 + read(
          elle::WeakBuffer(buffer.mutable_contents() + size1, size - size1),
          size - size1, offset + size1);
      }
      // multi case
      total_size = _file._header.size;
      block_size = _file._header.block_size;
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
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      _last_read_block = start_block;
      _check_prefetch();
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      { // single block case
        off_t block_offset = offset - (off_t)start_block * (off_t)block_size;
        auto it = _blocks.find(start_block);
        std::shared_ptr<elle::Buffer> block;
        if (it != _blocks.end())
        {
          ELLE_DEBUG("obtained block %s from cache", start_block);
          reactor::wait(it->second.ready);
          if (!it->second.block) // FIXME
            throw rfs::Error(EIO, elle::sprintf("lookahead failed"));
          block = it->second.block;
          it->second.last_use = std::chrono::high_resolution_clock::now();
        }
        else
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
          check_cache();
        }
        ELLE_ASSERT_LTE(signed(block_offset + size), block_size);
        if (signed(block->size()) < signed(block_offset + size))
        { // sparse file, eof shrinkage of size was handled above
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
      { // overlaps two blocks case
        ELLE_ASSERT(start_block == end_block - 1);
        int64_t second_size = (offset + size) % block_size; // second block
        int64_t first_size = size - second_size;
        int64_t second_offset = (int64_t)end_block * (int64_t)block_size;
        ELLE_DEBUG("split %s %s into %s %s and %s %s",
            size, offset, first_size, offset, second_size, second_offset);
        int r1 = read(elle::WeakBuffer(buffer.mutable_contents(), first_size),
                      first_size, offset + _file._data.size());
        if (r1 <= 0)
          return r1;
        int r2 = read(elle::WeakBuffer(buffer.mutable_contents() + first_size, second_size),
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
      if (size == 0)
        return 0;
      ELLE_TRACE_SCOPE("%s: write %s bytes at offset %s", *this, size, offset);
      ELLE_ASSERT_EQ(buffer.size(), size);
      this->_dirty = true;
      _file._header.mtime = time(nullptr);
      if (offset < signed(default_first_block_size))
      { // write on first block
        _fat_changed = true;
        auto wend = std::min(uint64_t(size + offset), default_first_block_size);
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
        auto& sz = File::_size_map.at(this->_file.address()).first;
        sz = std::max(sz, this->_file._header.size);
        return to_write + write(
          elle::ConstWeakBuffer(buffer.contents() + to_write, size - to_write),
          size - to_write, offset + to_write);
      }
      this->_file._header.size = std::max(this->_file._header.size,
                                            uint64_t(offset + size));
      auto& sz = File::_size_map.at(this->_file.address()).first;
      sz = std::max(sz, this->_file._header.size);
      // In case we skipped embeded first block, fill it
      this->_file._data.size(default_first_block_size);
      offset -= default_first_block_size;
      uint64_t const block_size = this->_file._header.block_size;
      int const start_block = offset / block_size;
      int const end_block = (offset + size - 1) / block_size;
      if (start_block == end_block)
        return this->_write_multi_single(
            std::move(buffer), offset, start_block);
      else
        return this->_write_multi_multi(
            std::move(buffer), offset, start_block, end_block);
    }

    int
    FileHandle::_write_multi_single(elle::ConstWeakBuffer buffer,
                                    off_t offset,
                                    int block_idx)
    {
      auto const block_size = this->_file._header.block_size;
      auto const size = buffer.size();
      std::shared_ptr<elle::Buffer> block;
      auto const it = _blocks.find(block_idx);
      if (it != _blocks.end())
      {
        block = it->second.block;
        it->second.dirty = true;
        it->second.last_use = std::chrono::high_resolution_clock::now();
      }
      else
      {
        block = _block_at(block_idx, true);
        ELLE_ASSERT(block);
        check_cache();
        auto const it = _blocks.find(block_idx);
        ELLE_ASSERT(it != _blocks.end());
        it->second.dirty = true;
        it->second.last_use = std::chrono::high_resolution_clock::now();
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
    FileHandle::_write_multi_multi(elle::ConstWeakBuffer buffer,
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
      int r1 = this->_write_multi_single(buffer.range(0, first_size),
          offset, start_block);
      if (r1 <= 0)
        return r1;
      int r2 = this->_write_multi_single(buffer.range(first_size),
          second_offset, end_block);
      if (r2 < 0)
        return r2;
      return r1 + r2;
    }

    void
    FileHandle::ftruncate(off_t new_size)
    {
      off_t current = _file._header.size;
      if (new_size == signed(current))
        return;
      if (new_size > signed(current))
      {
        char buf[16384] = {0};
        while (current < new_size)
        {
          auto nsz = std::min(off_t(16384), new_size - current);
          current += write(elle::WeakBuffer(buf, nsz), nsz, current);
        }
        return;
      }
      uint64_t first_block_size = _file._data.size();
      for (int i = _file._fat.size()-1; i >= 0; --i)
      {
        auto offset = first_block_size + i * _file._header.block_size;
        if (signed(offset) >= new_size)
        {
          // kick the block
          unchecked_remove(_model, _file._fat[i].first);
          _file._fat.pop_back();
          _blocks.erase(i);
        }
        else if (signed(offset + _file._header.block_size) >= new_size)
        { // truncate the block
          auto targetsize = new_size - offset;
          auto it = _blocks.find(i);
          if (it != _blocks.end())
          {
            auto data = it->second.block;
            data->size(targetsize);
            it->second.dirty = true;
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
      _commit_all();
    }

    void
    FileHandle::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "FileHandle(%x, \"%s\")",
                    (void*)(this), this->_file._address);
    }
    std::shared_ptr<elle::Buffer>
    FileHandle::_block_at(int index, bool create)
    {
      ELLE_ASSERT_GTE(index, 0);
      auto it = this->_blocks.find(index);
      if (it != this->_blocks.end())
      {
        it->second.last_use = std::chrono::high_resolution_clock::now();
        return it->second.block;
      }
      if (_file._fat.size() <= unsigned(index))
      {
        ELLE_TRACE("%s: block_at(%s) out of range", *this, index);
        if (!create)
        {
          return nullptr;
        }
        _file._fat.resize(index+1, FileData::FatEntry(Address::null, {}));
      }
      std::shared_ptr<elle::Buffer> b;
      if (_file._fat[index].first == Address::null)
      {
        b = std::make_shared<elle::Buffer>();
      }
      else
      {
        ELLE_TRACE("Fetching %s", index);
        Address addr(this->_file._fat[index].first.value(),
                     model::flags::immutable_block, false);
        auto block = fetch_or_die(_model, addr);
        auto crypted = block->take_data();
        cryptography::SecretKey sk(_file._fat[index].second);
        b = std::make_shared<elle::Buffer>(sk.decipher(crypted));
      }

      auto inserted = this->_blocks.insert(std::make_pair(index,
        CacheEntry{b, false}));
      inserted.first->second.ready.open();
      inserted.first->second.last_use = std::chrono::high_resolution_clock::now();
      inserted.first->second.dirty = false; // we just fetched or inserted it
      return inserted.first->second.block;
    }
    void
    FileHandle::_check_prefetch()
    {
      // Check if we need to relaunch a prefetcher
      int nidx = _last_read_block + 1;
      for (; nidx < _last_read_block + lookahead_blocks
        && _prefetchers_count < max_lookahead_threads; ++nidx)
      {
        if (nidx >= signed(_file._fat.size()))
          break;
        if (this->_blocks.find(nidx) == this->_blocks.end())
        {
          _prefetch(nidx);
          break;
        }
      }
    }

    void
    FileHandle::_prefetch(int idx)
    {
      ELLE_TRACE("%s: prefetch index %s", *this, idx);
      auto inserted =
        this->_blocks.insert(std::make_pair(idx, CacheEntry{}));
      inserted.first->second.last_use = std::chrono::high_resolution_clock::now();
      inserted.first->second.dirty = false;
      auto addr = Address(this->_file._fat[idx].first.value(),
                          model::flags::immutable_block, false);
      auto key = _file._fat[idx].second;
      ++_prefetchers_count;
      new reactor::Thread("prefetcher", [this, addr, idx, key] {
          std::unique_ptr<model::blocks::Block> bl;
          try
          {
            bl = fetch_or_die(_model, addr);
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
          auto b = std::make_shared<elle::Buffer>(
            cryptography::SecretKey(key).decipher(crypted));
          this->_blocks[idx].last_use = std::chrono::high_resolution_clock::now();
          this->_blocks[idx].block = b;
          this->_blocks[idx].ready.open();
          --this->_prefetchers_count;
          this->_check_prefetch();
          this->check_cache(this->max_cache_size);
      }, true);
    }
    void
    FileHandle::_commit_first()
    {
      _file.write(_model, WriteTarget::data | WriteTarget::times,
                  DirectoryData::null_block, _first_block_new);
      _first_block_new = false;
    }
    void
    FileHandle::_commit_all()
    {
      ELLE_TRACE_SCOPE("%s: commit all", this);
      if (!check_cache(0))
      {
        ELLE_DEBUG_SCOPE(
          "store first block with payload %s, fat %s, total_size %s",
          this->_file._data.size(),
          this->_file._fat,
          this->_file._header.size);
        this->_commit_first();
      }
    }
    bool
    FileHandle::_flush_block(int id)
    {
      bool fat_change = false;
      auto it = this->_blocks.find(id);
      Address prev = Address::null;
      if (_file._fat.size() < unsigned(id))
        prev = _file._fat.at(id).first;
      auto key = cryptography::random::generate<elle::Buffer>(32).string();
      auto ab = it->second.block;
      elle::Buffer cdata;
      if (ab->size() >= 262144)
        reactor::background([&] {
            cdata = cryptography::SecretKey(key).encipher(*ab);
        });
      else
        cdata = cryptography::SecretKey(key).encipher(*ab);
      auto block = _model.make_block<ImmutableBlock>(std::move(cdata), _file._address);
      auto baddr = block->address();
      _model.store(std::move(block), model::STORE_INSERT);
      if (baddr != prev)
      {
        ELLE_DEBUG("Changing address of block %s: %s -> %s", it->first,
          prev, baddr);
        fat_change = true;
        _file._fat[id] = FileData::FatEntry(baddr, key);
        if (prev != Address::null)
        {
          unchecked_remove(_model, prev);
        }
      }
      it->second.dirty = false;
      return fat_change;
    }

    bool
    FileHandle::check_cache(int cache_size)
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
      while (this->_blocks.size() > unsigned(cache_size))
      {
        auto it = std::min_element(this->_blocks.begin(), this->_blocks.end(),
          [](Elem const& a, Elem const& b) -> bool
          {
            if (a.second.last_use == b.second.last_use)
              return a.first < b.first;
            else
              return a.second.last_use < b.second.last_use;
          });
        ELLE_TRACE("Removing block %s from cache", it->first);
        if (cache_size == 0)
        { // final flush, sync
          if (it->second.dirty)
          {
            _fat_changed = _flush_block(it->first) || _fat_changed;
          }
          this->_blocks.erase(it);
        }
        else
        {
          if (it->second.dirty)
          {
            int id = it->first;
            ELLE_TRACE("starting async flusher for %s", id);
            auto ab = it->second.block;
            _flushers.emplace_back(
              new reactor::Thread("flusher", [this, id, ab] {
                auto key = cryptography::random::generate<elle::Buffer>(32).string();
                Address old_addr = Address::null;
                if (_file._fat.size() > unsigned(id))
                  old_addr = _file._fat.at(id).first;
                elle::Buffer cdata;
                if (ab->size() >= 262144)
                  reactor::background([&] {
                    cdata = cryptography::SecretKey(key).encipher(*ab);
                  });
                else
                  cdata = cryptography::SecretKey(key).encipher(*ab);
                auto block = _model.make_block<ImmutableBlock>(std::move(cdata), _file._address);
                auto baddr = block->address();
                _model.store(std::move(block), model::STORE_INSERT);
                if (baddr != old_addr)
                {
                  ELLE_DEBUG("Changing address of block %s: %s -> %s", id,
                    old_addr, baddr);
                  _fat_changed = true;
                  this->_file._fat[id] = FileData::FatEntry(baddr, key);
                  if (old_addr != Address::null)
                  {
                    unchecked_remove(_model, old_addr);
                  }
                }
            }, reactor::Thread::managed = true));
          }
          this->_blocks.erase(it);
        }
      }
      bool prev = this->_fat_changed;
      if (this->_fat_changed)
      {
        ELLE_DEBUG_SCOPE("FAT with %s entries changed, commit first block",
                         this->_file._fat.size());
        this->_commit_first();
        _first_block_new = false;
        _fat_changed = false;
      }
      return prev;
    }
  }
}
