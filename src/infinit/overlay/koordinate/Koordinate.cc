#include <infinit/overlay/koordinate/Koordinate.hh>

namespace infinit
{
  namespace overlay
  {
    namespace koordinate
    {
      /*-------------.
      | Construction |
      `-------------*/

      Koordinate::Koordinate(
        model::doughnut::Doughnut* dht,
        std::shared_ptr<infinit::model::doughnut::Local> local,
        Backends backends)
        : Overlay(dht, std::move(local))
        , _backends(std::move(backends))
      {
        this->_validate();
      }

      Koordinate::~Koordinate()
      {}

      void
      Koordinate::_validate() const
      {
        if (this->_backends.empty())
          elle::err("Koordinate backends list should not be empty");
      }

      /*------.
      | Peers |
      `------*/

      void
      Koordinate::_discover(NodeLocations const& peers)
      {
        for (auto& backend: this->_backends)
          backend->discover(peers);
      }

      /*-------.
      | Lookup |
      `-------*/

      reactor::Generator<std::pair<model::Address, Overlay::WeakMember>>
      Koordinate::_lookup(std::vector<model::Address> const& addrs, int n) const
      {
        this->_validate();
        return (*begin(this->_backends))->lookup(addrs, n);
      }

      reactor::Generator<Overlay::WeakMember>
      Koordinate::_lookup(model::Address address, int n, Operation op) const
      {
        this->_validate();
        return (*begin(this->_backends))->lookup(address, n, op);
      }

      Overlay::WeakMember
      Koordinate::_lookup_node(model::Address address) const
      {
        this->_validate();
        return (*begin(this->_backends))->lookup_node(address);
      }
    }
  }
}
