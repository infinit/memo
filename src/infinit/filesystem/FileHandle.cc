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
          if (offset >= signed(block->data().size()) - header_size)
          {
            ELLE_DEBUG("read past end: offset=%s, size=%s", offset,
                       block->data().size() - header_size);
            return 0;
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
        auto cpy = this->_owner->_first_block->clone();
        this->_owner->_owner.store_or_die(std::move(cpy),
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
