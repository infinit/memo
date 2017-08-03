#pragma once

#include <unordered_map>
#include <chrono>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

#include <memo/model/blocks/MutableBlock.hh>
#include <memo/model/doughnut/Consensus.hh>

namespace memo
{
  namespace bfs = boost::filesystem;

  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        namespace bmi = boost::multi_index;
        class Cache
          : public StackedConsensus
        {
        public:
          Cache(std::unique_ptr<Consensus> backend,
                boost::optional<int> cache_size = {},
                elle::DurationOpt cache_invalidation = {},
                elle::DurationOpt cache_ttl = {},
                boost::optional<bfs::path> disk_cache_path = {},
                boost::optional<uint64_t> disk_cache_size = {});
          ~Cache() override;

        /*--------.
        | Factory |
        `--------*/
        public:
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     boost::optional<boost::asio::ip::address> listen_address,
                     std::unique_ptr<silo::Silo> storage) override;

        /*----------.
        | Consensus |
        `----------*/
        protected:
          void
          _store(std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          std::unique_ptr<blocks::Block>
          _fetch(Address address, boost::optional<int> local_version) override;
          void
          _fetch(std::vector<AddressVersion> const& addresses,
                 std::function<void(Address, std::unique_ptr<blocks::Block>,
                                    std::exception_ptr)> res) override;
          void
          _remove(Address address, blocks::RemoveSignature rs) override;

        /*-----------.
        | Monitoring |
        `-----------*/
        public:
          elle::json::Object
          redundancy() override;
          elle::json::Object
          stats() override;

        /*------.
        | Cache |
        `------*/
        public:
          /// Clear all cached blocks.
          void
          clear();
          void
          insert(std::unique_ptr<blocks::Block> b);
        private:
          void
          _cleanup();
          std::unique_ptr<blocks::Block>
          _fetch_cache(Address address, boost::optional<int> local_version,
                       bool& hit, bool cache_only = false);
          void
          _insert_cache(blocks::Block& b);
          std::unique_ptr<blocks::Block>
          _copy(blocks::Block& block);
          ELLE_ATTRIBUTE_R(elle::Duration, cache_invalidation);
          ELLE_ATTRIBUTE_R(elle::Duration, cache_ttl);
          ELLE_ATTRIBUTE_R(int, cache_size);
          ELLE_ATTRIBUTE_R(boost::optional<bfs::path>, disk_cache_path);
          ELLE_ATTRIBUTE_R(uint64_t, disk_cache_size);
          class CachedBlock
          {
          public:
            CachedBlock(std::unique_ptr<blocks::Block> block);
            Address
            address() const;
            ELLE_ATTRIBUTE_RX(std::unique_ptr<blocks::Block>, block);
            ELLE_ATTRIBUTE_RW(elle::Time, last_used);
            ELLE_ATTRIBUTE_RW(elle::Time, last_fetched);
          };
          /// Sort mutable blocks first, ordered by last_fetched
          struct LastFetch
          {
            bool
            operator ()(CachedBlock const& lhs, CachedBlock const& rhs) const;
          };

          using BlockCache = bmi::multi_index_container<
            CachedBlock,
            bmi::indexed_by<
              bmi::hashed_unique<
                bmi::const_mem_fun<
                  CachedBlock,
                  Address, &CachedBlock::address> >,
              bmi::ordered_non_unique<
                bmi::const_mem_fun<
                  CachedBlock,
                  elle::Time const&, &CachedBlock::last_used> >,
              bmi::ordered_non_unique<
                bmi::const_mem_fun<
                  CachedBlock,
                  elle::Time const&, &CachedBlock::last_fetched> >
            > >;
          ELLE_ATTRIBUTE(BlockCache, cache);
          class CachedCHB
          {
          public:
            CachedCHB(Address address, uint64_t size, elle::Time last_used);
            ELLE_ATTRIBUTE_R(Address, address);
            ELLE_ATTRIBUTE_R(uint64_t, size);
            ELLE_ATTRIBUTE_RW(elle::Time, last_used);
          };
          using CHBDiskCache = bmi::multi_index_container<
            CachedCHB,
            bmi::indexed_by<
              bmi::hashed_unique<
                bmi::const_mem_fun<
                  CachedCHB,
                  Address const&, &CachedCHB::address> >,
              bmi::ordered_non_unique<
                bmi::const_mem_fun<
                  CachedCHB,
                  elle::Time const&, &CachedCHB::last_used> >
          > >;
          ELLE_ATTRIBUTE(CHBDiskCache, disk_cache);
          ELLE_ATTRIBUTE(uint64_t, disk_cache_used);
          ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr, cleanup_thread);
        private:
          void _load_disk_cache();
          void _disk_cache_push(blocks::Block& block);
          using Pending
            = std::unordered_map<Address, std::shared_ptr<elle::reactor::Barrier>>;
          ELLE_ATTRIBUTE(Pending, pending);
        };
      }
    }
  }
}
