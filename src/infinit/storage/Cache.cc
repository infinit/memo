#include <elle/log.hh>

#include <infinit/storage/Cache.hh>
#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>

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
    {
    }

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
        _init();
        if (this->_keys->find(k) == this->_keys->end())
          throw MissingKey(k);
        auto data =  this->_storage->get(k);
        this->_blocks.emplace(k, data);
        return data;
      }
    }

    void
    Cache::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_TRACE_SCOPE("%s _set %s", *this, k);
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
        _init();
        this->_blocks.emplace(k, value);
        this->_keys->insert(k);
      }
      ELLE_TRACE("%s _set done %s", *this, k);
    }

    void
    Cache::_erase(Key k)
    {
      _init();
      ELLE_TRACE("%s: drop %s", *this, k);
      this->_blocks.erase(k);
      this->_storage->erase(k);
      this->_keys->erase(k);
    }

    void
    Cache::_init() const
    {
      if (this->_keys)
        return;
      ELLE_TRACE("%s: sync keys", *this);
      auto keys = this->_storage->list();
      this->_keys = Keys();
      this->_keys->insert(keys.begin(), keys.end());
    }
    std::vector<Key>
    Cache::_list()
    {
      _init();
      return std::vector<Key>(this->_keys->begin(), this->_keys->end());
    }
  }
}
