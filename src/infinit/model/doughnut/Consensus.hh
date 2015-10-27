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
        : public elle::Printable
      {
      public:
        Consensus(Doughnut& doughnut);
        virtual ~Consensus() {}
      protected:
        Doughnut& _doughnut;

      /*-------.
      | Blocks |
      `-------*/
      public:
        void
        store(overlay::Overlay& overlay,
              std::unique_ptr<blocks::Block> block,
              StoreMode mode,
              std::unique_ptr<ConflictResolver> resolver);
        std::unique_ptr<blocks::Block>
        fetch(overlay::Overlay& overlay, Address address);
        void
        remove(overlay::Overlay& overlay, Address address);
      protected:
        virtual
        void
        _store(overlay::Overlay& overlay,
               std::unique_ptr<blocks::Block> block,
               StoreMode mode,
               std::unique_ptr<ConflictResolver> resolver);
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(overlay::Overlay& overlay, Address address);
        virtual
        void
        _remove(overlay::Overlay& overlay, Address address);
        std::shared_ptr<Peer>
        _owner(overlay::Overlay& overlay,
               Address const& address,
               overlay::Operation op) const;

      /*----------.
      | Printable |
      `----------*/
      public:
        void
        print(std::ostream&) const override;
      };
    }
  }
}

#endif
