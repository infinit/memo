#include <infinit/overlay/Overlay.hh>

#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Overlay");

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

    void
    Overlay::register_local(std::shared_ptr<model::doughnut::Local> local)
    {}

    Overlay::Members
    Overlay::lookup(model::Address address, int n, Operation op, bool strict) const
    {
      ELLE_TRACE_SCOPE("%s: lookup %s nodes for %s", *this, n, address);
      auto res = this->_lookup(address, n, op);
      if (strict && signed(res.size()) != n)
        throw elle::Error(elle::sprintf("%s: got only %s of the %s requested nodes",
                                        *this, res.size(), n));
      return res;
    }

    Overlay::Member
    Overlay::lookup(model::Address address, Operation op) const
    {
      return this->lookup(address, 1, op)[0];
    }
  }
}
