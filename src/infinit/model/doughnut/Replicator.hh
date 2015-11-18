#ifndef INFINIT_MODEL_DOUGHNUT_REPLICATOR_HH
# define INFINIT_MODEL_DOUGHNUT_REPLICATOR_HH

# include <boost/filesystem.hpp>

# include <reactor/thread.hh>

# include <infinit/model/doughnut/Consensus.hh>


namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        class Replicator
          : public Consensus
        {
        public:
          Replicator(Doughnut& doughnut, int factor,
                     boost::filesystem::path const& journal_dir,
                     bool rereplicate);
          ~Replicator();
          ELLE_ATTRIBUTE_R(int, factor);
        protected:
          virtual
          void
          _store(overlay::Overlay& overlay,
                 std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(overlay::Overlay& overlay, Address address,
                 boost::optional<int>) override;
          virtual
          void
          _remove(overlay::Overlay& overlay, Address address) override;
          void
          _process_cache();
          void
          _process_loop();
          std::unique_ptr<blocks::Block>
          _vote(overlay::Overlay::Members peers, Address address);
          overlay::Overlay* _overlay;
          boost::filesystem::path _journal_dir;
          reactor::Thread _process_thread;
          std::unordered_map<Address, int> _retries;
          int _frame;
          bool _rereplicate;
        };
      }
    }
  }
}

#endif
