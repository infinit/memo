#include <infinit/storage/Memory.hh>

#include <elle/log.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.Memory")

namespace infinit
{
  namespace storage
  {
    elle::Buffer
    Memory::_get(Key key) const
    {
      ELLE_TRACE_SCOPE("%s: get %x", *this, key);
      auto it = this->_blocks.find(key);
      if (it == this->_blocks.end())
        throw MissingKey(key);
      auto& buffer = it->second;
      return elle::Buffer(buffer.contents(), buffer.size());
    }

    void
    Memory::_set(Key key, elle::Buffer value, bool insert, bool update)
    {
      ELLE_ASSERT(insert || update);
      ELLE_TRACE_SCOPE("%s: %s at %x", *this,
                       insert ? update ? "upsert" : "insert" : "update", key);
      if (insert)
      {
        auto insertion =
          this->_blocks.insert(std::make_pair(key, elle::Buffer()));
        if (!insertion.second && !update)
          throw Collision(key);
        insertion.first->second = std::move(value);
        if (insert && update && insertion.second)
          ELLE_DEBUG("%s: block inserted", *this);
        else if (insert && update && insertion.second)
          ELLE_DEBUG("%s: block updated", *this);
      }
      else
      {
        auto search = this->_blocks.find(key);
        if (search == this->_blocks.end())
          throw MissingKey(key);
        else
          search->second = std::move(value);
      }
    }
  }
}
