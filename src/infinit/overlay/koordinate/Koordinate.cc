#include <infinit/overlay/koordinate/Koordinate.hh>

#include <elle/make-vector.hh>

#include <boost/algorithm/cxx11/all_of.hpp>

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
      Koordinate::_cleanup()
      {
        for (auto& backend: this->_backends)
          backend->cleanup();
      }

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

      bool
      Koordinate::_discovered(model::Address id)
      {
        // Any or all ? Better safe than sorry, if someone expects the overlay
        // to have discovered a peer, make sure all backends have discovered it.
        return boost::algorithm::all_of(this->_backends,
                   [&] (Backend const& b) { return b->discovered(id); });
      }

      /*-------.
      | Lookup |
      `-------*/

      auto
      Koordinate::_allocate(model::Address address,
                            int n) const
        -> MemberGenerator
      {
        this->_validate();
        return (*begin(this->_backends))->allocate(address, n);
      }

      auto
      Koordinate::_lookup(std::vector<model::Address> const& addrs, int n) const
        -> LocationGenerator
      {
        this->_validate();
        return (*begin(this->_backends))->lookup(addrs, n);
      }

      auto
      Koordinate::_lookup(model::Address address,
                          int n, bool fast) const
        -> MemberGenerator
      {
        this->_validate();
        return (*begin(this->_backends))->lookup(address, n, fast);
      }

      auto
      Koordinate::_lookup_node(model::Address address) const
        -> WeakMember
      {
        this->_validate();
        return (*begin(this->_backends))->lookup_node(address);
      }

      /*-----------.
      | Monitoring |
      `-----------*/

      std::string
      Koordinate::type_name() const
      {
        return "koordinate";
      }

      elle::json::Array
      Koordinate::peer_list() const
      {
        elle::json::Array res;
        for (auto const& backend: this->_backends)
        {
          auto const& peer_list = backend->peer_list();
          res.insert(res.begin(), peer_list.begin(), peer_list.end());
        }
        return res;
      }

      elle::json::Object
      Koordinate::stats() const
      {
        return
          {
            {"type", this->type_name()},
            {"types", elle::make_vector(this->_backends,
                                        [](auto const& backend)
                                        {
                                          return backend->type_name();
                                        })},
            };
      }
    }
  }
}
