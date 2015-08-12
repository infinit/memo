#include <elle/log.hh>

#include <infinit/storage/Cache.hh>
#include <infinit/storage/Collision.hh>

ELLE_LOG_COMPONENT("infinit.storage.Cache");

namespace infinit
{
  namespace storage
  {
    Cache::Cache(std::unique_ptr<Storage> backend,
                 boost::optional<int> size)
      : _storage(std::move(backend))
      , _size(std::move(size))
      , _keys()
    {}

    elle::Buffer
    Cache::_get(Key k) const
    {
      auto it = this->_blocks.find(k);
      if (it != this->_blocks.end())
      {
        ELLE_TRACE_SCOPE("%s: cache hit on %s", *this, k);
        return it->second;
      }
      else
      {
        ELLE_TRACE_SCOPE("%s: cache miss on %s", *this, k);
        auto data =  this->_storage->get(k);
        this->_blocks.emplace(k, data);
        return data;
      }
    }

    void
    Cache::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      auto it = this->_blocks.find(k);
      if (it != this->_blocks.end() && !insert)
        throw Collision(k);
      this->_storage->set(k, value, insert, update);
      if (it != this->_blocks.end())
      {
        ELLE_TRACE("%s: update %s", *this, k);
        it->second = elle::Buffer(value);
      }
      else
      {
        ELLE_TRACE("%s: cache %s", *this, k);
        this->_blocks.emplace(k, value);
      }
    }

    void
    Cache::_erase(Key k)
    {
      ELLE_TRACE("%s: drop %s", *this, k);
      this->_blocks.erase(k);
      return this->_storage->erase(k);
    }

    std::vector<Key>
    Cache::_list()
    {
      if (!this->_keys)
      {
        ELLE_TRACE("%s: sync keys", *this);
        this->_keys = this->_storage->list();
      }
      return this->_keys.get();
    }
  }
}
