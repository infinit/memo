#ifndef INFINIT_OVERLAY_STONEHENGE_HH
# define INFINIT_OVERLAY_STONEHENGE_HH

# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace overlay
  {
    class Stonehenge
      : public Overlay
    {
    /*-------------.
    | Construction |
    `-------------*/
    public:
      Stonehenge(Members members);
      ELLE_ATTRIBUTE_R(Members, members);

    /*-------.
    | Lookup |
    `-------*/
    protected:
      virtual
      Members
      _lookup(model::Address address, int n, Operation op) const override;
    };

    struct StonehengeOverlayConfig
      : public OverlayConfig
    {
      std::vector<std::string> nodes;
      StonehengeOverlayConfig();
      StonehengeOverlayConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s);
      virtual
      std::unique_ptr<infinit::overlay::Overlay>
      make(bool observer) override;
    };
  }
}

#endif
