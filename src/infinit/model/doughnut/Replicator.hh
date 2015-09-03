#ifndef INFINIT_MODEL_DOUGHNUT_REPLICATOR_HH
# define INFINIT_MODEL_DOUGHNUT_REPLICATOR_HH

# include <infinit/model/doughnut/Consensus.hh>


namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Replicator: public Consensus
      {
      public:
        Replicator(Doughnut& doughnut, int factor);
        ELLE_ATTRIBUTE_R(int, factor);
      protected:
        virtual
        void
        _store(overlay::Overlay& overlay, blocks::Block& block, StoreMode mode) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(overlay::Overlay& overlay, Address address) override;
        virtual
        void
        _remove(overlay::Overlay& overlay, Address address) override;
      };
    }
  }
}

#endif