#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH

# include <memory>

# include <infinit/model/Model.hh>
# include <infinit/model/doughnut/Peer.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Doughnut // Doughnut. DougHnuT. Get it ?
        : public Model
      {
      public:
        Doughnut(std::vector<std::unique_ptr<Peer>> peers);
      protected:
        virtual
        std::unique_ptr<blocks::Block>
        _make_block() const override;
        virtual
        void
        _store(blocks::Block& block) override;
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address) const override;
        virtual
        void
        _remove(Address address) override;
        ELLE_ATTRIBUTE_R(std::vector<std::unique_ptr<Peer>>, peers);
      private:
        Peer&
        _owner(Address const& address) const;
      };
    }
  }
}

#endif
