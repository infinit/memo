#ifndef INFINIT_MODEL_DOUGHNUT_CACHE_HH
# define INFINIT_MODEL_DOUGHNUT_CACHE_HH

# include <unordered_map>
# include <chrono>

# include <boost/multi_index_container.hpp>
# include <boost/multi_index/hashed_index.hpp>
# include <boost/multi_index/identity.hpp>
# include <boost/multi_index/mem_fun.hpp>
# include <boost/multi_index/ordered_index.hpp>
# include <boost/multi_index/sequenced_index.hpp>

# include <infinit/model/blocks/MutableBlock.hh>
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
                boost::optional<std::chrono::seconds> cache_ttl = {},
                boost::optional<boost::filesystem::path> disk_cache_path = {},
                boost::optional<uint64_t> disk_cache_size = {}
                );
          ~Cache();

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
          _store(std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(Address address, boost::optional<int> local_version) override;
          virtual
          void
          _fetch(std::vector<AddressVersion> const& addresses,
                 std::function<void(Address, std::unique_ptr<blocks::Block>,
                                    std::exception_ptr)> res) override;
          virtual
          void
          _remove(Address address, blocks::RemoveSignature rs) override;

        /*------.
        | Cache |
        `------*/
        public:
          /// Clear all cached blocks.
          void
          clear();
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
          ELLE_ATTRIBUTE_R(std::unique_ptr<Consensus>, backend);
          ELLE_ATTRIBUTE_R(std::chrono::seconds, cache_invalidation);
          ELLE_ATTRIBUTE_R(std::chrono::seconds, cache_ttl);
          ELLE_ATTRIBUTE_R(int, cache_size);
          ELLE_ATTRIBUTE_R(boost::optional<boost::filesystem::path>, disk_cache_path);
          ELLE_ATTRIBUTE_R(uint64_t, disk_cache_size);
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
          /// Sort mutable blocks first, ordered by last_fetched
          struct LastFetch
          {
            bool
            operator ()(CachedBlock const& lhs, CachedBlock const& rhs) const;
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
          class CachedCHB
          {
          public:
            CachedCHB(Address address, uint64_t size, clock::time_point last_used);
            ELLE_ATTRIBUTE_R(Address, address);
            ELLE_ATTRIBUTE_R(uint64_t, size);
            ELLE_ATTRIBUTE_RW(clock::time_point, last_used);
          };
          typedef bmi::multi_index_container<
            CachedCHB,
            bmi::indexed_by<
              bmi::hashed_unique<
                bmi::const_mem_fun<
                  CachedCHB,
                  Address const&, &CachedCHB::address> >,
              bmi::ordered_non_unique<
                bmi::const_mem_fun<
                  CachedCHB,
                  clock::time_point const&, &CachedCHB::last_used> >
          > > CHBDiskCache;
          ELLE_ATTRIBUTE(CHBDiskCache, disk_cache);
          ELLE_ATTRIBUTE(uint64_t, disk_cache_used);
          ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, cleanup_thread);
        private:
          void _load_disk_cache();
          void _disk_cache_push(blocks::Block& block);
          typedef std::unordered_map<Address, std::shared_ptr<reactor::Barrier>>
          Pending;
          ELLE_ATTRIBUTE(Pending, pending);
        };
      }
    }
  }
}

#endif
