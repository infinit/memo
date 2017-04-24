#include <elle/find.hh>
#include <elle/log.hh>

#include <infinit/storage/Cache.hh>
#include <infinit/storage/Collision.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.Cache");

namespace infinit
{
  namespace storage
  {
    namespace
    {
      /// Do not let Storage::erase try to update the size: we don't
      /// know yet what the difference will be.  Metrics will be
      /// updated by the worker.
      constexpr auto metrics_are_deferred = 0;
    }

    // FIXME: Cache::_storage vs. Async::_backend and Crypt::_backend.
    Cache::Cache(std::unique_ptr<Storage> backend,
                 boost::optional<int> size,
                 bool use_list,
                 bool use_status)
      : _storage(std::move(backend))
      , _size(std::move(size))
      , _use_list(use_list)
      , _use_status(use_status)
    {
      // Update our metrics when our backend updates its.
      _storage->register_notifier([this]
                       {
                         this->_update_metrics();
                       });
      ELLE_TRACE("Cache start, list=%s, status=%s", _use_list, _use_status);
    }

    void
    Cache::_update_metrics()
    {
      // _erase and _set are expected to return deltas, that are
      // applied by Storage::erase and Storage::set.  But we don't
      // know these deltas, as the operations are asynchronous.
      //
      // Rather, _erase and _set should return 0, so that the metrics
      // are unchanged when operations are queued.  But we hook the
      // metrics notifications from the worker and then propagate
      // them.
      //
      // They are no locking issues, as they are atomics.
      _usage = _storage->usage().load();
      _block_count = _storage->block_count().load();
      _notify_metrics();
    }

    elle::Buffer
    Cache::_get(Key k) const
    {
      if (auto it = find(this->_blocks, k))
      {
        ELLE_TRACE_SCOPE("%s: cache hit on %s", *this, k);
        return it->second;
      }
      else
      {
        ELLE_TRACE_SCOPE("%s: cache miss on %s", *this, k);
        if (_use_list && (_init(), !elle::find(*this->_keys, k))
            || _use_status && this->_storage->status(k) == BlockStatus::missing)
          throw MissingKey(k);
        auto data = this->_storage->get(k);
        this->_blocks.emplace(k, data);
        return data;
      }
    }

    int
    Cache::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_TRACE_SCOPE("%s _set %s", *this, k);
      auto it = elle::find(this->_blocks, k);
      if (it && !update)
        throw Collision(k);
      this->_storage->set(k, value, insert, update);
      if (it)
      {
        ELLE_TRACE("%s: update %s", *this, k);
        it->second = elle::Buffer(value);
      }
      else
      {
        ELLE_TRACE("%s: cache %s", *this, k);
        this->_blocks.emplace(k, value);
        if (_use_list)
        {
          _init();
          this->_keys->insert(k);
        }
      }
      ELLE_TRACE("%s _set done %s", *this, k);
      return metrics_are_deferred;
    }

    int
    Cache::_erase(Key k)
    {
      ELLE_TRACE("%s: drop %s", *this, k);
      this->_blocks.erase(k);
      this->_storage->erase(k);
      if (_use_list)
      {
        _init();
        this->_keys->erase(k);
      }
      return metrics_are_deferred;
    }

    void
    Cache::_init() const
    {
      if (_use_list && !this->_keys)
      {
        ELLE_TRACE("%s: sync keys", *this);
        auto keys = this->_storage->list();
        this->_keys = Keys(keys.begin(), keys.end());
      }
    }

    std::vector<Key>
    Cache::_list()
    {
      if (_use_list)
      {
        _init();
        return {this->_keys->begin(), this->_keys->end()};
      }
      else
        return _storage->list();
    }
  }
}
