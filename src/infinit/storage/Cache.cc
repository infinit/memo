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
                 boost::optional<int> size,
                 bool use_list,
                 bool use_status)
      : _storage(std::move(backend))
      , _size(std::move(size))
      , _use_list(use_list)
      , _use_status(use_status)
      , _keys()
    {
      ELLE_TRACE("Cache start, list=%s, status=%s", _use_list, _use_status);
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
        if (_use_list && this->_keys->find(k) == this->_keys->end())
          throw MissingKey(k);
        if (_use_status && this->_storage->status(k) == BlockStatus::missing)
          throw MissingKey(k);
        auto data =  this->_storage->get(k);
        this->_blocks.emplace(k, data);
        return data;
      }
    }

    int
    Cache::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_TRACE_SCOPE("%s _set %s", *this, k);
      auto it = this->_blocks.find(k);
      if (it != this->_blocks.end() && !update)
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
        if (_use_list)
          this->_keys->insert(k);
      }
      ELLE_TRACE("%s _set done %s", *this, k);
      // FIXME: impl.
      return 0;
    }

    int
    Cache::_erase(Key k)
    {
      _init();
      ELLE_TRACE("%s: drop %s", *this, k);
      this->_blocks.erase(k);
      this->_storage->erase(k);
      if (_use_list)
        this->_keys->erase(k);
      // FIXME: impl.
      return 0;
    }

    void
    Cache::_init() const
    {
      if (!_use_list || this->_keys)
        return;
      ELLE_TRACE("%s: sync keys", *this);
      auto keys = this->_storage->list();
      this->_keys = Keys();
      this->_keys->insert(keys.begin(), keys.end());
    }

    std::vector<Key>
    Cache::_list()
    {
      if (_use_list)
      {
        _init();
        return std::vector<Key>(this->_keys->begin(), this->_keys->end());
      }
      else
        return _storage->list();
    }
  }
}
