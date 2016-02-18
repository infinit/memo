#include <infinit/filesystem/FileHandle.hh>
#include <infinit/filesystem/Directory.hh>
#include <elle/cast.hh>
#include <elle/serialization/binary.hh>
#include <infinit/model/doughnut/Doughnut.hh>

#include <infinit/model/MissingBlock.hh>

ELLE_LOG_COMPONENT("infinit.filesystem.FileHandle");

namespace infinit
{
  namespace filesystem
  {
    FileHandle::FileHandle(std::shared_ptr<File> owner,
        bool writable,
        bool push_mtime,
        bool no_fetch,
        bool dirty)
      : _owner(owner)
      , _dirty(dirty)
      , _writable(writable)
    {
      ELLE_TRACE_SCOPE("%s: create (previous handle count = %s,%s)",
          *this, _owner->_r_handle_count, _owner->_rw_handle_count);
      if (_writable)
        _owner->_rw_handle_count++;
      else
        _owner->_r_handle_count++;
      elle::SafeFinally reset_handle([&] {
          if (_writable)
            _owner->_rw_handle_count--;
          else
            _owner->_r_handle_count--;
      });
      _owner->_header.atime = time(nullptr);
      // FIXME: the only thing that can invalidate _owner is hard links
      // keep tracks of open handle to know if we should refetch
      // or a backend stat call?
      if (!no_fetch && _owner->_r_handle_count + _owner->_rw_handle_count == 1)
      {
        try
        {
          auto address = _owner->_parent->_files.at(_owner->_name).second;
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
            ELLE_WARN("%s: block missing in model and not in cache", *this);
            throw;
          }
        }
        catch (infinit::model::doughnut::ValidationFailed const& e)
        {
          ELLE_TRACE("%s: validation failed: %s", *this, e);
          THROW_ACCES;
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("%s: unexpected elle exception while fetching: %s",
              *this, e.what());
          _owner->_first_block.reset();
          throw rfs::Error(EIO, e.what());
        }
        // FIXME: I *really* don't like those.
        catch (std::exception const& e)
        {
          ELLE_ERR("%s: unexpected exception while fetching: %s",
              *this, e.what());
          throw rfs::Error(EIO, e.what());
        }
        catch (...)
        {
          ELLE_ERR("%s: unkown while fetching", *this);
          throw rfs::Error(EIO, "unkown error");
        }
      }
      reset_handle.abort();
    }

    FileHandle::~FileHandle()
    {
      if (_writable)
        --this->_owner->_rw_handle_count;
      else
        --this->_owner->_r_handle_count;
      ELLE_TRACE_SCOPE("%s: close, %s,%s remain", *this,
        this->_owner->_r_handle_count, this->_owner->_rw_handle_count);
      if (this->_owner->_r_handle_count + this->_owner->_rw_handle_count == 0)
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
        this->_owner->_commit_all();
      }
      else
        ELLE_DEBUG("%s: skip non-dirty flush", *this);
    }

    int
    FileHandle::read(elle::WeakBuffer buffer, size_t size, off_t offset)
    {
      _owner->_ensure_first_block();
      ELLE_TRACE_SCOPE("%s: read %s at %s", *this, size, offset);
      ELLE_TRACE("%s: have %s bytes and %s fat entries totalling %s", *this,
        _owner->_data.size(), _owner->_fat.size(), _owner->_header.size);
      ELLE_ASSERT_EQ(buffer.size(), size);
      int64_t total_size;
      int32_t block_size;
      if (offset < signed(_owner->_data.size()))
      {
        size_t size1 = std::min(size, size_t(_owner->_data.size() - offset));
        memcpy(buffer.mutable_contents(),
               this->_owner->_data.contents() + offset,
               size1);
        if (size1 == size || _owner->_fat.empty())
          return size1;
        return size1 + read(
          elle::WeakBuffer(buffer.mutable_contents() + size1, size - size1),
          size - size1, offset + size1);
      }
      // multi case
      total_size = _owner->_header.size;
      block_size = _owner->_header.block_size;
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
      offset -= File::first_block_size;
      off_t end = offset + size;
      int start_block = offset ? (offset) / block_size : 0;
      _owner->_last_read_block = start_block;
      _owner->_check_prefetch();
      int end_block = end ? (end - 1) / block_size : 0;
      if (start_block == end_block)
      { // single block case
        off_t block_offset = offset - (off_t)start_block * (off_t)block_size;
        auto it = _owner->_blocks.find(start_block);
        std::shared_ptr<AnyBlock> block;
        if (it != _owner->_blocks.end())
        {
          ELLE_DEBUG("obtained block %s from cache", start_block);
          reactor::wait(it->second.ready);
          if (!it->second.block) // FIXME
            throw rfs::Error(EIO, elle::sprintf("lookahead failed"));
          ELLE_DEBUG("block at %x", it->second.block.get());
          ELLE_DEBUG("block %x with %s bytes",
                     it->second.block->address(),
                     it->second.block->data().size());
          block = it->second.block;
          it->second.last_use = std::chrono::system_clock::now();
        }
        else
        {
          block = _owner->_block_at(start_block, false);
          if (!block)
          {
            if (_owner->_rw_handle_count == 0)
            { // attempt a fetch
              _owner->_fetch();
              block = _owner->_block_at(start_block, false);
            }
            if (!block)
            {
              // block would have been allocated: sparse file?
              memset(buffer.mutable_contents(), 0, size);
              ELLE_TRACE("read %s 0-bytes, missing block %s", size, start_block);
              return size;
            }
          }
          ELLE_DEBUG("fetched block %x of size %s", block->address(), block->data().size());
          _owner->check_cache();
        }
        ELLE_ASSERT_LTE(signed(block_offset + size), block_size);
        if (signed(block->data().size()) < signed(block_offset + size))
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
                      first_size, offset + File::first_block_size);
        if (r1 <= 0)
          return r1;
        int r2 = read(elle::WeakBuffer(buffer.mutable_contents() + first_size, second_size),
                      second_size, second_offset + File::first_block_size);
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
      _owner->_header.mtime = time(nullptr);
      if (offset < signed(File::first_block_size))
      { // write on first block
        this->_owner->_fat_changed = true;
        auto wend = std::min(uint64_t(size + offset), File::first_block_size);
        if (_owner->_data.size() < wend)
        {
          auto oldsz = _owner->_data.size();
          _owner->_data.size(wend);
          memset(_owner->_data.mutable_contents() + oldsz,
                 0,
                 wend - oldsz);
        }
        auto to_write = wend - offset;
        memcpy(_owner->_data.mutable_contents() + offset,
               buffer.contents(),
               to_write);
        this->_owner->_header.size = std::max(this->_owner->_header.size,
                                              uint64_t(offset + size));
        return to_write + write(
          elle::WeakBuffer(buffer.mutable_contents() + to_write, size - to_write),
          size - to_write, offset + to_write);
      }
      this->_owner->_header.size = std::max(this->_owner->_header.size,
                                            uint64_t(offset + size));
      offset -= File::first_block_size;
      uint64_t const block_size = this->_owner->_header.block_size;
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
    FileHandle::_write_multi_single(
        elle::WeakBuffer buffer, off_t offset, int block_idx)
    {
      auto const block_size = this->_owner->_header.block_size;
      auto const size = buffer.size();
      std::shared_ptr<AnyBlock> block;
      auto const it = this->_owner->_blocks.find(block_idx);
      if (it != this->_owner->_blocks.end())
      {
        block = it->second.block;
        it->second.dirty = true;
        it->second.last_use = std::chrono::system_clock::now();
      }
      else
      {
        block = this->_owner->_block_at(block_idx, true);
        ELLE_ASSERT(block);
        this->_owner->check_cache();
        auto const it = this->_owner->_blocks.find(block_idx);
        ELLE_ASSERT(it != this->_owner->_blocks.end());
        it->second.dirty = true;
        it->second.last_use = std::chrono::system_clock::now();
      }
      off_t block_offset = offset % block_size;
      if (block->data().size() < block_offset + size)
      {
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
      return size;
    }

    int
    FileHandle::_write_multi_multi(
        elle::WeakBuffer buffer, off_t offset, int start_block, int end_block)
    {
      uint64_t const block_size = this->_owner->_header.block_size;
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
      ELLE_TRACE_SCOPE("%s: fsync", *this);
    }

    void
    FileHandle::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "FileHandle(%x, \"%s\")",
                    (void*)(this), this->_owner->_name);
    }
  }
}
