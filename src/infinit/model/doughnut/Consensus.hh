#ifndef INFINIT_MODEL_DOUGHNUT_CONSENSUS_HH
# define INFINIT_MODEL_DOUGHNUT_CONSENSUS_HH

# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Consensus
      {
      public:
        Consensus(Doughnut& doughnut);
        virtual ~Consensus() {}
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut);

      public:
        void
        store(overlay::Overlay& overlay, blocks::Block& block, StoreMode mode);
        std::unique_ptr<blocks::Block>
        fetch(overlay::Overlay& overlay, Address address);
        void
        remove(overlay::Overlay& overlay, Address address);

      protected:
        virtual
        void
        _store(overlay::Overlay& overlay, blocks::Block& block, StoreMode mode);
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(overlay::Overlay& overlay, Address address);
        virtual
        void
        _remove(overlay::Overlay& overlay, Address address);

      private:
        std::shared_ptr<Peer>
        _owner(overlay::Overlay& overlay,
               Address const& address,
               overlay::Operation op) const;
      };
    }
  }
}

#endif
