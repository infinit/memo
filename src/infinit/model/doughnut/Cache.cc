#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>

#include <elle/serialization/json.hh>
#include <elle/serialization/binary.hh>
#include <elle/bench.hh>

#include <infinit/model/doughnut/OKB.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Cache");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        static
        Cache::clock::time_point
        now()
        {
          return Cache::clock::now();
        }

        Cache::Cache(Doughnut& doughnut,
                     std::unique_ptr<Consensus> backend,
                     boost::optional<int> cache_size,
                     boost::optional<std::chrono::seconds> cache_invalidation,
                     boost::optional<std::chrono::seconds> cache_ttl)
          : Consensus(doughnut)
          , _backend(std::move(backend))
          , _cache_invalidation(
            cache_invalidation ?
            cache_invalidation.get() : std::chrono::seconds(15))
          , _cache_ttl(
            cache_ttl ?
            cache_ttl.get() : std::chrono::seconds(60 * 5))
          , _cache_size(cache_size ? cache_size.get() : 64000000)
        {
          ELLE_TRACE_SCOPE("%s: create with size %s and TTL %ss",
                           *this, this->_cache_size, this->_cache_ttl.count());
        }

        Cache::~Cache()
        {}

        void
        Cache::_remove(overlay::Overlay& overlay, Address address)
        {
          ELLE_TRACE_SCOPE("%s: remove %s", *this, address);
          this->_cleanup();
          if (this->_cache.erase(address) > 0)
            ELLE_DEBUG("drop block from cache");
          else
            ELLE_DEBUG("block was not in cache");
          this->_backend->remove(overlay, address);
        }

        std::unique_ptr<blocks::Block>
        Cache::_fetch(overlay::Overlay& overlay,
                      Address address,
                      boost::optional<int> local_version)
        {
          ELLE_TRACE_SCOPE("%s: fetch %s", *this, address);
          static elle::Bench bench_hit("cache.hit", 10_sec);
          this->_cleanup();
          auto hit = this->_cache.find(address);
          if (hit != this->_cache.end())
          {
            ELLE_DEBUG("cache hit");
            this->_cache.modify(
              hit, [] (CachedBlock& b) { b.last_used(now()); });
            bench_hit.add(1);
            if (local_version)
              if (auto mb =
                  dynamic_cast<blocks::MutableBlock*>(hit->block().get()))
                if (mb->version() == local_version.get())
                  return nullptr;
            return hit->block()->clone();
          }
          else
          {
            ELLE_DEBUG("cache miss");
            bench_hit.add(0);
            auto res = _backend->fetch(overlay, address, local_version);
            this->_cache.emplace(res->clone());
            return res;
          }
        }

        void
        Cache::_store(overlay::Overlay& overlay,
                      std::unique_ptr<blocks::Block> block,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          ELLE_TRACE_SCOPE("%s: store %s", *this, block->address());
          this->_cleanup();
          this->_backend->store(
            overlay, block->clone(), mode, std::move(resolver));
          auto hit = this->_cache.find(block->address());
          if (hit != this->_cache.end())
            this->_cache.modify(
              hit, [&] (CachedBlock& b) {
                b.block() = std::move(block);
                b.last_used(now());
              });
          else
            this->_cache.emplace(std::move(block));
        }

        void
        Cache::_cleanup()
        {
          ELLE_TRACE_SCOPE("%s: cleanup cache", *this);
          auto& order = this->_cache.get<1>();
          auto deadline = now() - this->_cache_ttl;
          auto it = order.begin();
          while (it != order.end() &&
                 it->last_used() < deadline)
          {
            ELLE_DUMP("evict %s from cache", it->block()->address());
            it = order.erase(it);
          }
          // FIXME: take cache_size in account too
        }

        Cache::CachedBlock::CachedBlock(std::unique_ptr<blocks::Block> block)
          : _block(std::move(block))
          , _last_used(now())
        {}

        Address
        Cache::CachedBlock::address() const
        {
          return this->_block->address();
        }
      }
    }
  }
}
