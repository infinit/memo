#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HH

# include <memory>

# include <cryptography/KeyPair.hh>

# include <infinit/model/Model.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Doughnut // Doughnut. DougHnuT. Get it ?
        : public Model
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Doughnut(infinit::cryptography::KeyPair keys,
                 std::unique_ptr<overlay::Overlay> overlay);
        ELLE_ATTRIBUTE(std::unique_ptr<overlay::Overlay>, overlay)
        ELLE_ATTRIBUTE_R(infinit::cryptography::KeyPair, keys);

      protected:
        virtual
        std::unique_ptr<blocks::MutableBlock>
        _make_mutable_block() const override;
        virtual
        std::unique_ptr<blocks::ImmutableBlock>
        _make_immutable_block(elle::Buffer content) const override;
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
        friend class Local;

      private:
        std::unique_ptr<Peer>
        _owner(Address const& address) const;
      };
    }
  }
}

#endif
