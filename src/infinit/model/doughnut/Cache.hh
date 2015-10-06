#ifndef INFINIT_MODEL_DOUGHNUT_CACHE_HH
# define INFINIT_MODEL_DOUGHNUT_CACHE_HH

# include <unordered_map>
# include <chrono>
# include <infinit/model/doughnut/Consensus.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Cache
      : public Consensus
      {
        public:
          using Clock = std::chrono::high_resolution_clock;
          typedef Clock::time_point TimePoint;
          typedef Clock::duration Duration;
          Cache(Doughnut& doughnut, std::unique_ptr<Consensus> backend,
                Duration mut_cache_ttl = std::chrono::seconds(1),
                long const_cache_size = 50);
          ~Cache();

        protected:
          virtual
          void
          _store(overlay::Overlay& overlay,
                 blocks::Block& block,
                 StoreMode mode,
                 ConflictResolver resolver) override;
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(overlay::Overlay& overlay,
                 Address address) override;
          virtual
          void
          _remove(overlay::Overlay& overlay,
                  Address address) override;

          private:
            void _cleanup();
            std::unique_ptr<blocks::Block> _copy(blocks::Block& block);
            std::unique_ptr<Consensus> _backend;
            Duration _mut_cache_ttl;
            int _const_cache_size;
            // Use a LRU cache for ImmutableBlock, and a TTL cache for MutableBlock
            std::map<TimePoint, Address> _mut_cache_time;
            std::map<TimePoint, Address> _const_cache_time;
            std::unordered_map<Address,
              std::pair<TimePoint, std::unique_ptr<blocks::Block>>> _cache;
      };
    }
  }
}




#endif