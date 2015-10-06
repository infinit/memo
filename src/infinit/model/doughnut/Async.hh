#ifndef INFINIT_MODEL_DOUGHNUT_ASYNC_HH
# define INFINIT_MODEL_DOUGHNUT_ASYNC_HH

# include <boost/optional.hpp>
# include <infinit/model/doughnut/Consensus.hh>
# include <reactor/Channel.hh>
# include <reactor/thread.hh>
# include <functional>
# include <unordered_map>
# include <sstream>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Async
        : public Consensus
      {
        public:
          Async(Doughnut& doughnut, std::unique_ptr<Consensus> backend);
          virtual ~Async();

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
          void _process_loop();

          std::unique_ptr<blocks::Block> _copy(blocks::Block& block) const;

          struct Op
          {
            Op(overlay::Overlay& overlay_,
               Address addr_,
               std::unique_ptr<blocks::Block>&& block_,
               boost::optional<StoreMode> mode_ = {},
               ConflictResolver resolver_ = {})
              : overlay(overlay_)
              , addr{addr_}
              , block{std::move(block_)}
              , mode{std::move(mode_)}
              , resolver{resolver_}
            {}

            overlay::Overlay& overlay;
            Address addr;
            std::unique_ptr<blocks::Block> block;
            boost::optional<StoreMode> mode;
            ConflictResolver resolver;
          };

          std::unique_ptr<Consensus> _backend;
          reactor::Thread _process_thread;
          reactor::Channel<Op> _ops;

          // This map contains for a given address the last version of each
          // block.
          std::unordered_map<Address, blocks::Block*> _last;
      };

    } // namespace doughnut
  } // namespace model
} // namsepace infinit

#endif /* !INFINIT_MODEL_DOUGHNUT_ASYNC_HH */
