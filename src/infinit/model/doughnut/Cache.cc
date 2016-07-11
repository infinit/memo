#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>

#include <elle/bench.hh>
#include <elle/bytes.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/json.hh>
#include <elle/serialization/binary.hh>

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
          , _cache_size(cache_size ? cache_size.get() : 64_mB)
          , _disk_cache_path(disk_cache_path)
          , _disk_cache_size(
            disk_cache_size ? disk_cache_size.get() : 512_mB)
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
          ELLE_TRACE_SCOPE("%s: remove %f", this, address);
          if (this->_cache.erase(address) > 0)
            ELLE_DEBUG("drop block from cache");
          else
          {
            auto it = this->_disk_cache.find(address);
            if (it != this->_disk_cache.end())
            {
              ELLE_DEBUG("drop block from disk cache");
              this->_disk_cache_used -= it->size();
              boost::system::error_code erc;
              auto path = *this->_disk_cache_path / elle::sprintf("%x", it->address());
              boost::filesystem::remove(path, erc);
              if (erc)
                ELLE_WARN("Error pruning %s from cache: %s", path, erc);
              this->_disk_cache.erase(it);
            }
            else
              ELLE_DEBUG("block was not in cache");
          }
          this->_backend->remove(address, std::move(rs));
        }

        void
        Cache::_fetch(std::vector<AddressVersion> const& addresses,
                      std::function<void(Address, std::unique_ptr<blocks::Block>,
                                         std::exception_ptr)> res)
        {
          std::vector<AddressVersion> missing;
          for (auto a: addresses)
          {
            bool hit = false;
            auto block = this->_fetch_cache(a.first, a.second, hit, true);
            if (hit)
              res(a.first, std::move(block), {});
            else
              missing.push_back(a);
          }
          // Don't pass local_version to fetch, prioritizing cache feed over
          // this optimization.
          for (auto& av: missing)
            av.second.reset();
          _backend->fetch(missing,
            [&](Address addr, std::unique_ptr<blocks::Block> block,
                std::exception_ptr exc)
            {
              if (block)
              {
                this->_insert_cache(*block);
              }
              res(addr, std::move(block), exc);
            });
        }

        std::unique_ptr<blocks::Block>
        Cache::_fetch(Address address, boost::optional<int> local_version)
        {
          bool hit = false;
          auto res = _fetch_cache(address, local_version, hit);
          return res;
        }

        void
        Cache::_insert_cache(blocks::Block& b)
        {
          if (!b.validate(doughnut(), false))
          {
            ELLE_WARN("%s: invalid block received for %s", this, b.address());
            throw elle::Error("invalid block");
          }
          static bool decode = elle::os::getenv("INFINIT_NO_PREEMPT_DECODE", "").empty();
          if (decode)
            try
            {
              static elle::Bench bench("bench.cache.preempt_decode", 10000_sec);
              elle::Bench::BenchScope bs(bench);
              b.data();
            }
            catch (elle::Error const& e)
            {
              ELLE_TRACE("%s: block %f is not readable: %s", this, b.address(), e);
            }
          if (this->_disk_cache_size &&
            dynamic_cast<blocks::ImmutableBlock*>(&b))
          this->_disk_cache_push(b);
          else if (dynamic_cast<blocks::MutableBlock*>(&b))
            this->_cache.emplace(b.clone());

        }
        std::unique_ptr<blocks::Block>
        Cache::_fetch_cache(Address address, boost::optional<int> local_version,
                            bool& cache_hit, bool cache_only)
        {
          cache_hit = false;
          ELLE_TRACE_SCOPE("%s: fetch %f (local_version: %s)",
                           this, address, local_version);
          static elle::Bench bench_hit("bench.cache.ram.hit", 1000_sec);
          static elle::Bench bench_disk_hit("bench.cache.disk.hit", 1000_sec);
          static elle::Bench bench("bench.cache._fetch", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          auto hit = this->_cache.find(address);
          if (hit != this->_cache.end())
          {
            cache_hit = true;
            ELLE_DEBUG("cache hit");
            this->_cache.modify(
              hit, [] (CachedBlock& b) { b.last_used(now()); });
            bench_hit.add(1);
            if (local_version)
              if (auto mb =
                  dynamic_cast<blocks::MutableBlock*>(hit->block().get()))
              {
                auto version = mb->version();
                if (version == local_version.get())
                {
                  ELLE_DEBUG("cached version is the same: %s", version);
                  return nullptr;
                }
                else
                  ELLE_DEBUG("cached version is more recent: %s", version);
              }
            return hit->block()->clone();
          }
          else
          {
            bench_hit.add(0);
            // try disk cache
            auto disk_hit = this->_disk_cache.find(address);
            if (disk_hit != this->_disk_cache.end())
            {
              cache_hit = true;
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
            if (cache_only)
              return {};
            auto it = this->_pending.find(address);
            if (it != this->_pending.end())
            {
              static elle::Bench bench("bench.cache.pending_wait", 10000_sec);
              elle::Bench::BenchScope bs(bench);
              ELLE_TRACE("%s: fetch on %f pending", this, address);
              auto b = it->second;
              b->wait();
              return _fetch(address, local_version);
            }
            it = this->_pending.insert(std::make_pair(
              address, std::make_shared<reactor::Barrier>())).first;
            auto b = it->second;
            elle::SafeFinally sf([&]
              {
                b->open();
                this->_pending.erase(address);
              });
            // Don't pass local_version to fetch, prioritizing cache feed over
            // this optimization.
            auto res = _backend->fetch(address);
            // FIXME: pass the whole block to fetch() so we can cache it there ?
            if (res)
            {
              this->_insert_cache(*res);
              auto mut = dynamic_cast<blocks::MutableBlock*>(res.get());
              if (mut && local_version && mut->version() == *local_version)
                return nullptr;
            }
            return res;
          }
          return {};
        }

        void
        Cache::_store(std::unique_ptr<blocks::Block> block,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          static elle::Bench bench("bench.cache.store", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          ELLE_TRACE_SCOPE("%s: store %f", this, block->address());
          auto mb = dynamic_cast<blocks::MutableBlock*>(block.get());
          std::unique_ptr<blocks::Block> cloned;
          {
            static elle::Bench bench("bench.cache.store.clone", 10000_sec);
            elle::Bench::BenchScope bs(bench);
            // Block was necessarily validated on its way up, or generated
            // locally.
            if (mb)
              mb->validated(true);
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
            this->_disk_cache_push(*cloned);
          }
        }

        void
        Cache::_disk_cache_push(blocks::Block& block)
        {
          if (!this->_disk_cache_path)
            return;
          auto path = *this->_disk_cache_path
            / elle::sprintf("%x", block.address());
          {
            boost::filesystem::ofstream ofs(path, std::ios::binary);
            elle::serialization::binary::SerializerOut sout(ofs);
            sout.set_context<Doughnut*>(&this->doughnut());
            sout.serialize_forward(&block);
          }
          auto sz = boost::filesystem::file_size(path);
          this->_disk_cache.emplace(CachedCHB{block.address(), sz, now()});
          this->_disk_cache_used += sz;
          ELLE_DEBUG("add %f to disk cache (%s bytes)", block.address(), sz);
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
            {
              static elle::Bench bench("bench.cache.cleanup", 10000_sec);
              elle::Bench::BenchScope bs(bench);
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
                std::vector<Model::AddressVersion> need_refresh;
                for (auto it = order.begin(); it != order.end(); ++it)
                {
                  auto& cached = *it;
                  if (!(cached.last_fetched() < deadline))
                    break;
                  auto const address = cached.block()->address();
                  if (auto mb =
                      dynamic_cast<blocks::MutableBlock*>(cached.block().get()))
                  {
                    need_refresh.push_back(std::make_pair(address, mb->version()));
                  }
                  else
                  {
                    ELLE_WARN("Nonmutable block %f in Cache", address);
                    break;
                  }
                }
                static const int batch_size = std::stoi(
                  elle::os::getenv("INFINIT_CACHE_REFRESH_BATCH_SIZE", "20"));
                for (int i=0; i < signed(need_refresh.size()); i+= batch_size)
                {
                  std::vector<Model::AddressVersion> batch;
                  if (signed(need_refresh.size()) >= (i + batch_size))
                    batch.insert(batch.end(), need_refresh.begin() + i,
                      need_refresh.begin() + i + batch_size);
                  else
                    batch.insert(batch.end(), need_refresh.begin() + i,
                      need_refresh.end());
                  ELLE_DEBUG("Processing batch %s-%s of %s",
                    i, i+batch.size(), need_refresh.size());
                  this->_backend->fetch(batch,
                    [&](Address a, std::unique_ptr<blocks::Block> b,
                      std::exception_ptr e)
                    {
                      if (e)
                      {
                        ELLE_TRACE("fetch error on %f: %s",
                                   a, elle::exception_string(e));
                        this->_cache.erase(a);
                      }
                      else
                      {
                        auto it = this->_cache.find(a);
                        if (it != this->_cache.end())
                          this->_cache.modify(
                            it,
                            [&] (CachedBlock& cache)
                            {
                              if (b)
                                cache.block() = std::move(b);
                              cache.last_fetched(now);
                            });
                      }
                    });
                  }
                }
              }
            reactor::sleep(
              boost::posix_time::seconds(
                this->_cache_invalidation.count()) / 10);
          }
        }

        Cache::CachedCHB::CachedCHB(Address address,
                                    uint64_t size,
                                    clock::time_point last_used)
          : _address(address)
          , _size(size)
          , _last_used(last_used)
        {}

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
