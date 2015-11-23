#ifndef INFINIT_MODEL_DOUGHNUT_CACHE_HH
# define INFINIT_MODEL_DOUGHNUT_CACHE_HH

# include <unordered_map>
# include <chrono>

# include <boost/multi_index_container.hpp>
# include <boost/multi_index/hashed_index.hpp>
# include <boost/multi_index/ordered_index.hpp>
# include <boost/multi_index/sequenced_index.hpp>

# include <infinit/model/doughnut/Consensus.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        namespace bmi = boost::multi_index;
        class Cache
          : public Consensus
        {
        public:
          using clock = std::chrono::high_resolution_clock;
          Cache(std::unique_ptr<Consensus> backend,
                boost::optional<int> cache_size = {},
                boost::optional<std::chrono::seconds> cache_invalidation = {},
                boost::optional<std::chrono::seconds> cache_ttl = {});
          ~Cache();
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     std::unique_ptr<storage::Storage> storage) override;

        /*--------.
        | Factory |
        `--------*/
        public:
          virtual
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     std::unique_ptr<storage::Storage> storage) override;

        /*----------.
        | Consensus |
        `----------*/
        protected:
          virtual
          void
          _store(overlay::Overlay& overlay,
                 std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(overlay::Overlay& overlay,
                 Address address,
                 boost::optional<int> local_version) override;
          virtual
          void
          _remove(overlay::Overlay& overlay,
                  Address address) override;

        private:
          void _cleanup();
          std::unique_ptr<blocks::Block> _copy(blocks::Block& block);
          std::unique_ptr<Consensus> _backend;
          ELLE_ATTRIBUTE_R(std::chrono::seconds, cache_invalidation);
          ELLE_ATTRIBUTE_R(std::chrono::seconds, cache_ttl);
          ELLE_ATTRIBUTE_R(int, cache_size);
          // Use a LRU cache for ImmutableBlock, and a TTL cache for
          // MutableBlock
          class CachedBlock
          {
          public:
            CachedBlock(std::unique_ptr<blocks::Block> block);
            Address
            address() const;
            ELLE_ATTRIBUTE_RX(std::unique_ptr<blocks::Block>, block);
            ELLE_ATTRIBUTE_RW(clock::time_point, last_used);
            ELLE_ATTRIBUTE_RW(clock::time_point, last_fetched);
          };
          typedef bmi::multi_index_container<
            CachedBlock,
            bmi::indexed_by<
              bmi::hashed_unique<
                bmi::const_mem_fun<
                  CachedBlock,
                  Address, &CachedBlock::address> >,
              bmi::ordered_non_unique<
                bmi::const_mem_fun<
                  CachedBlock,
                  clock::time_point const&, &CachedBlock::last_used> >,
              bmi::ordered_non_unique<
                bmi::const_mem_fun<
                  CachedBlock,
                  clock::time_point const&, &CachedBlock::last_fetched> >
            > > BlockCache;
          ELLE_ATTRIBUTE(BlockCache, cache);
        };
      }
    }
  }
}

#endif
