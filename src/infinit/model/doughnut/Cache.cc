#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>

#include <elle/serialization/json.hh>
#include <elle/serialization/binary.hh>
#include <elle/bench.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
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

        Cache::Cache(std::unique_ptr<Consensus> backend,
                     boost::optional<int> cache_size,
                     boost::optional<std::chrono::seconds> cache_invalidation,
                     boost::optional<std::chrono::seconds> cache_ttl,
                     boost::optional<boost::filesystem::path> disk_cache_path,
                     boost::optional<uint64_t> disk_cache_size)
          : Consensus(backend->doughnut())
          , _backend(std::move(backend))
          , _cache_invalidation(
            cache_invalidation ?
            cache_invalidation.get() : std::chrono::seconds(15))
          , _cache_ttl(
            cache_ttl ?
            cache_ttl.get() : std::chrono::seconds(60 * 5))
          , _cache_size(cache_size ? cache_size.get() : 64000000)
          , _disk_cache_path(disk_cache_path)
          , _disk_cache_size(disk_cache_size ? disk_cache_size.get() : 64000000)
          , _disk_cache_used(0)
          , _cleanup_thread(
            new reactor::Thread(elle::sprintf("%s cleanup", *this),
                                [this] { this->_cleanup();}))
        {
          ELLE_TRACE_SCOPE(
            "%s: create with size %s, TTL %ss and invalidation %ss",
            *this, this->_cache_size,
            this->_cache_ttl.count(), this->_cache_invalidation.count());
          if (!this->_disk_cache_path)
            this->_disk_cache_size = 0;
          if (this->_disk_cache_size)
          {
            boost::filesystem::create_directories(*this->_disk_cache_path);
            this->_load_disk_cache();
          }
        }

        Cache::~Cache()
        {}

        /*--------.
        | Factory |
        `--------*/

        std::unique_ptr<Local>
        Cache::make_local(boost::optional<int> port,
                          std::unique_ptr<storage::Storage> storage)
        {
          return
            this->_backend->make_local(std::move(port), std::move(storage));
        }

        /*----------.
        | Consensus |
        `----------*/

        void
        Cache::_remove(Address address, blocks::RemoveSignature rs)
        {
          ELLE_TRACE_SCOPE("%s: remove %s", *this, address);
          if (this->_cache.erase(address) > 0)
            ELLE_DEBUG("drop block from cache");
          else
            ELLE_DEBUG("block was not in cache");
          this->_backend->remove(address, std::move(rs));
        }

        std::unique_ptr<blocks::Block>
        Cache::_fetch(Address address, boost::optional<int> local_version)
        {
          ELLE_TRACE_SCOPE("%s: fetch %s", *this, address);
          static elle::Bench bench_hit("bench.cache.ram.hit", 1000_sec);
          static elle::Bench bench_disk_hit("bench.cache.disk.hit", 1000_sec);
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

            bench_hit.add(0);
            // try disk cache
            auto disk_hit = this->_disk_cache.find(address);
            if (disk_hit != this->_disk_cache.end())
            {
              ELLE_DEBUG("cache hit(disk)");
              bench_disk_hit.add(1);
              auto path = *this->_disk_cache_path / elle::sprintf("%x", address);
              boost::filesystem::ifstream is(path, std::ios::binary);
              elle::serialization::binary::SerializerIn sin(is);
              sin.set_context<Doughnut*>(&this->doughnut());
              auto block = sin.deserialize<std::unique_ptr<blocks::Block>>();
              this->_disk_cache.modify(disk_hit,
                [](CachedCHB& b) { b.last_used(now());});
              return block;
            }
            else
            {
              ELLE_DEBUG("cache miss");
              bench_disk_hit.add(0);
            }
            auto res = _backend->fetch(address, local_version);
            // FIXME: pass the whole block to fetch() so we can cache it there ?
            if (res)
            {
              if (dynamic_cast<blocks::MutableBlock*>(res.get()))
              {
                this->_cache.emplace(res->clone());
              }
              else if (_disk_cache_size)
              {
                this->_disk_cache_push(res);
              }
            }
            return res;
          }
        }

        void
        Cache::_store(std::unique_ptr<blocks::Block> block,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          static elle::Bench bench("bench.cache.store", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          ELLE_TRACE_SCOPE("%s: store %s", *this, block->address());
          auto mb = dynamic_cast<blocks::MutableBlock*>(block.get());
          std::unique_ptr<blocks::Block> cloned;
          {
            static elle::Bench bench("bench.cache.store.clone", 10000_sec);
            elle::Bench::BenchScope bs(bench);
            // Block was necessarily validated on its way up, so it's safe
            // to flag it as local
            if (mb)
              mb->is_local(true);
            cloned = block->clone();
          }
          auto address = block->address();
          {
            static elle::Bench bench("bench.cache.store.store", 10000_sec);
            elle::Bench::BenchScope bs(bench);
            this->_backend->store(
              std::move(block), mode, std::move(resolver));
          }
          if (mb)
          {
            auto hit = this->_cache.find(address);
            if (hit != this->_cache.end())
            {
              this->_cache.modify(
                hit, [&] (CachedBlock& b) {
                  b.block() = std::move(cloned);
                  b.last_used(now());
                  b.last_fetched(now());
                });
            }
            else
            {
              this->_cache.emplace(std::move(cloned));
            }
          }
          else
          {
            this->_disk_cache_push(cloned);
          }
        }

        void
        Cache::_disk_cache_push(std::unique_ptr<blocks::Block>& block)
        {
          auto path = *this->_disk_cache_path
            / elle::sprintf("%x", block->address());
          {
            boost::filesystem::ofstream ofs(path, std::ios::binary);
            elle::serialization::binary::SerializerOut sout(ofs);
            sout.set_context<Doughnut*>(&this->doughnut());
            sout.serialize_forward(block);
          }
          auto sz = boost::filesystem::file_size(path);
          this->_disk_cache.emplace(CachedCHB{block->address(), sz, now()});
          this->_disk_cache_used += sz;
          ELLE_DEBUG("add %f to disk cache (%s bytes)", block->address(), sz);
          while (this->_disk_cache_used > this->_disk_cache_size)
          {
            ELLE_ASSERT(!this->_disk_cache.empty());
            auto& order = this->_disk_cache.get<1>();
            auto it = order.begin();
            auto path = *this->_disk_cache_path / elle::sprintf("%x", it->address());
            ELLE_DEBUG("Pruning %s of size %s from cache", path, it->size());
            boost::system::error_code erc;
            this->_disk_cache_used -= it->size();
            boost::filesystem::remove(path, erc);
            if (erc)
              ELLE_WARN("Error pruning %s from cache: %s", path, erc);
            order.erase(it);
          }
        }

        /*------.
        | Cache |
        `------*/

        void
        Cache::_load_disk_cache()
        {
          if (!this->_disk_cache_path)
            return;
          ELLE_TRACE_SCOPE("%s: reload disk cache", this);
          int count = 0;
          for (auto it = boost::filesystem::directory_iterator(*this->_disk_cache_path);
              it != boost::filesystem::directory_iterator();
              ++it)
          {
            auto sz = boost::filesystem::file_size(it->path());
            auto addr = Address::from_string(it->path().filename().string().substr(2));
            this->_disk_cache.insert(CachedCHB{addr, sz, clock::time_point()});
            this->_disk_cache_used += sz;
            ++count;
          }
          ELLE_TRACE("loaded %s blocks totalling %s bytes",
                     count, this->_disk_cache_used);
        }

        void
        Cache::clear()
        {
          ELLE_TRACE_SCOPE("%s: clear", *this);
          this->_cache.clear();
        }

        void
        Cache::_cleanup()
        {
          while (true)
          {
            auto const now = consensus::now();
            ELLE_DEBUG_SCOPE("%s: cleanup cache", *this);
            ELLE_DEBUG("evict unused blocks")
            {
              auto& order = this->_cache.get<1>();
              auto deadline = now - this->_cache_ttl;
              auto it = order.begin();
              while (it != order.end() && it->last_used() < deadline)
              {
                ELLE_DUMP("evict %s", it->block()->address());
                it = order.erase(it);
              }
            }
            // FIXME: take cache_size in account too
            ELLE_DEBUG("refresh obsolete blocks")
            {
              auto& order = this->_cache.get<2>();
              auto deadline = now - this->_cache_invalidation;
              while (!order.empty())
              {
                auto& cached = *order.begin();
                if (!(cached.last_fetched() < deadline))
                  break;
                auto const address = cached.block()->address();
                if (auto mb =
                    dynamic_cast<blocks::MutableBlock*>(cached.block().get()))
                {
                  ELLE_DEBUG_SCOPE("refresh %s", address);
                  try
                  {
                    auto block = this->_backend->fetch(address, mb->version());
                    // Beware: everything is invalidated past there we probably
                    // yielded.
                    auto it = this->_cache.find(address);
                    if (it != this->_cache.end())
                      this->_cache.modify(
                        it,
                        [&] (CachedBlock& cache)
                        {
                          if (block)
                            cache.block() = std::move(block);
                          cache.last_fetched(now);
                        });
                  }
                  catch (MissingBlock const&)
                  {
                    ELLE_DUMP("drop removed block");
                    this->_cache.erase(address);
                  }
                  catch (elle::Error const& e)
                  {
                    ELLE_TRACE("Fetch error on %x: %s", address, e);
                    this->_cache.erase(address);
                  }
                }
                else
                  ELLE_WARN("Nonmutable block in Cache");
              }
            }
            reactor::sleep(
              boost::posix_time::seconds(
                this->_cache_invalidation.count()) / 10);
          }
        }

        Cache::CachedCHB::CachedCHB(Address address, uint64_t size, clock::time_point last_used)
        : _address(address)
        , _size(size)
        , _last_used(last_used)
        {
        }

        Cache::CachedBlock::CachedBlock(std::unique_ptr<blocks::Block> block)
          : _block(std::move(block))
          , _last_used(now())
          , _last_fetched(now())
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
