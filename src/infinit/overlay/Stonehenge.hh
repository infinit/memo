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
  }
}

#endif
