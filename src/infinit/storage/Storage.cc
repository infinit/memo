#include <infinit/storage/Storage.hh>

#include <elle/log.hh>

#include <infinit/storage/Key.hh>

ELLE_LOG_COMPONENT("infinit.storage.Storage");

namespace infinit
{
  namespace storage
  {
    elle::Buffer
    Storage::get(Key key) const
    {
      ELLE_TRACE_SCOPE("%s: get %x", *this, key);
      return this->_get(key);
    }

    void
    Storage::set(Key key, elle::Buffer value, bool insert, bool update)
    {
      ELLE_ASSERT(insert || update);
      ELLE_TRACE_SCOPE("%s: %s at %x", *this,
                       insert ? update ? "upsert" : "insert" : "update", key);
      return this->_set(key, std::move(value), insert, update);
    }

    void
    Storage::erase(Key key)
    {
      ELLE_TRACE_SCOPE("%s: erase %x", *this, key);
      return this->_erase(key);
    }
  }
}
