#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>

#include <elle/serialization/json.hh>
#include <elle/serialization/binary.hh>
#include <elle/bench.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Cache");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        static Cache::TimePoint now()
        {
          return Cache::Clock::now();
        }

        Cache::Cache(Doughnut& doughnut,
                     std::unique_ptr<Consensus> backend,
                     boost::optional<int> cache_size,
                     boost::optional<std::chrono::seconds> cache_ttl)
          : Consensus(doughnut)
          , _backend(std::move(backend))
          , _cache_ttl(cache_ttl ? cache_ttl.get() : std::chrono::seconds(8))
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
          this->_cleanup();
          auto hit = this->_cache.find(address);
          if (hit != this->_cache.end())
          {
            auto t = hit->second.first;
            if (dynamic_cast<blocks::ImmutableBlock*>(hit->second.second.get()))
              this->_const_cache_time.erase(t);
            else
              this->_mut_cache_time.erase(t);
            this->_cache.erase(hit);
          }
          this->_backend->remove(overlay, address);
        }

        std::unique_ptr<blocks::Block>
        Cache::_fetch(overlay::Overlay& overlay, Address address)
        {
          ELLE_TRACE_SCOPE("%s: fetch %s", *this, address);
          static elle::Bench bench_hit("cache.hit", 10_sec);
          this->_cleanup();
          auto hit = this->_cache.find(address);
          if (hit != this->_cache.end())
          {
            ELLE_DEBUG("cache hit");
            if (dynamic_cast<blocks::ImmutableBlock*>(hit->second.second.get()))
            {
              auto it = this->_const_cache_time.find(hit->second.first);
              ELLE_ASSERT(it != this->_const_cache_time.end());
              ELLE_ASSERT_EQ(address, it->second);
              this->_const_cache_time.erase(it);
              auto t = now();
              this->_const_cache_time.insert(std::make_pair(t, address));
              hit->second.first = t;
            }
            bench_hit.add(1);
            return hit->second.second->clone();
          }
          else
          {
            ELLE_DEBUG("cache miss");
            bench_hit.add(0);
            auto res = _backend->fetch(overlay, address);
            auto t = now();
            this->_cache.emplace(address, std::make_pair(t, res->clone()));
            if (dynamic_cast<blocks::ImmutableBlock*>(res.get()))
            {
              auto ir = _const_cache_time.insert(std::make_pair(t, address));
              ELLE_ASSERT(ir.second);
            }
            else
            {
              auto ir = _mut_cache_time.insert(std::make_pair(t, address));
              ELLE_ASSERT(ir.second);
            }
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
          auto hit = _cache.find(block->address());
          if (hit != _cache.end())
          {
            TimePoint t = hit->second.first;
            if (!dynamic_cast<blocks::ImmutableBlock*>(block.get()))
            {
              // Refresh.
              this->_mut_cache_time.erase(t);
              t = now();
              this->_mut_cache_time.insert(std::make_pair(t, block->address()));
              hit->second.first = t;
            }
            hit->second.second = block->clone();
          }
          else
          {
            auto t = now();
            if (dynamic_cast<blocks::ImmutableBlock*>(block.get()))
              _const_cache_time.insert(std::make_pair(t, block->address()));
            else
              _mut_cache_time.insert(std::make_pair(t, block->address()));
            this->_cache.emplace(block->address(),
                                 std::make_pair(t, block->clone()));
          }
          this->_backend->store(
            overlay, std::move(block), mode, std::move(resolver));
        }

        void
        Cache::_cleanup()
        {
          auto t = now();
          while (!_mut_cache_time.empty())
          {
            auto b = _mut_cache_time.begin();
            if (!(t - b->first < this->_cache_ttl))
            {
              _cache.erase(b->second);
              _mut_cache_time.erase(b);
            }
            else
              break;
          }
          while (signed(_const_cache_time.size()) > this->_cache_size)
          {
            auto b = _const_cache_time.begin();
            _cache.erase(b->second);
            _const_cache_time.erase(b);
          }
          ELLE_ASSERT_EQ(
            this->_cache.size(),
            this->_const_cache_time.size() + this->_mut_cache_time.size());
        }
      }
    }
  }
}
