#include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Overlay::Overlay()
      : _doughnut(nullptr)
    {}

    /*-------.
    | Lookup |
    `-------*/

    Overlay::Members
    Overlay::lookup(model::Address address, int n, Operation op) const
    {
      auto res = this->_lookup(address, n, op);
      ELLE_ASSERT_EQ(signed(res.size()), n);
      return res;
    }

    Overlay::Member
    Overlay::lookup(model::Address address, Operation op) const
    {
      return this->lookup(address, 1, op)[0];
    }
  }
}
