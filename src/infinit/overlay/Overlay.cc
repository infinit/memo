#include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace overlay
  {
    /*-------.
    | Lookup |
    `-------*/

    Overlay::Members
    Overlay::lookup(model::Address address, int n) const
    {
      auto res = this->_lookup(address, n);
      ELLE_ASSERT_EQ(signed(res.size()), n);
      return res;
    }

    Overlay::Member
    Overlay::lookup(model::Address address) const
    {
      return this->lookup(address, 1)[0];
    }
  }
}
