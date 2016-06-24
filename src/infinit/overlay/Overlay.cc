#include <elle/log.hh>

#include <reactor/Scope.hh>

#include <infinit/overlay/Overlay.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/DummyPeer.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Overlay");

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Overlay::Overlay(model::doughnut::Doughnut* dht,
                     std::shared_ptr<infinit::model::doughnut::Local> local,
                     model::Address node_id)
      : _node_id(std::move(node_id))
      , _doughnut(dht)
      , _local(local)
    {}

    Overlay::~Overlay()
    {
      ELLE_TRACE("%s: destruct", this);
    }

    elle::json::Json
    Overlay::query(std::string const& k, boost::optional<std::string> const& v)
    {
      return {};
    }

    /*------.
    | Peers |
    `------*/

    void
    Overlay::discover(NodeEndpoints const& peers)
    {
      ELLE_TRACE("%s: discover new peers: %s", this, peers);
      this->_discover(peers);
    }

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<std::pair<model::Address, Overlay::WeakMember>>
    Overlay::lookup(std::vector<model::Address> const& addresses, int n) const
    {
      ELLE_TRACE_SCOPE("%s: lookup %s nodes for %s", *this, n, addresses);
      return this->_lookup(addresses, n);
    }

    reactor::Generator<Overlay::WeakMember>
    Overlay::lookup(model::Address address, int n, Operation op) const
    {
      ELLE_TRACE_SCOPE("%s: lookup %s nodes for %f", this, n, address);
      return this->_lookup(address, n, op);
    }

    Overlay::WeakMember
    Overlay::lookup(model::Address address, Operation op) const
    {
      ELLE_TRACE_SCOPE("%s: lookup 1 node for %f", this, address);
      for (auto res: this->_lookup(address, 1, op))
        return res;
      throw model::MissingBlock(address);
    }

    reactor::Generator<std::pair<model::Address, Overlay::WeakMember>>
    Overlay::_lookup(std::vector<model::Address> const& addresses, int n) const
    {
      return reactor::Generator<std::pair<model::Address, WeakMember>>(
        [this, addresses, n] (reactor::Generator<std::pair<model::Address, WeakMember>>::yielder const& yield)
        {
          for (auto const& a: addresses)
            for (auto res: lookup(a, n, OP_FETCH))
              yield(std::make_pair(a, res));
        });
    }

    Configuration::Configuration(elle::serialization::SerializerIn& input)
    {
      this->serialize(input);
    }

    Overlay::WeakMember
    Overlay::lookup_node(model::Address address)
    {
      return this->_lookup_node(address);
    }

    reactor::Generator<Overlay::WeakMember>
    Overlay::lookup_nodes(std::unordered_set<model::Address> addresses)
    {
      return reactor::generator<Overlay::WeakMember>(
        [this, addresses]
        (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
        {
          elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
          {
            for (auto const& address: addresses)
              scope.run_background(
                elle::sprintf("%s: fetch node by address", *this),
                [&]
                {
                  try
                  {
                    yield(this->_lookup_node(address));
                  }
                  catch (elle::Error const& e)
                  {
                    ELLE_TRACE("%s: failed to lookup node %f: %s",
                               this, address, e);
                    yield(WeakMember(new model::doughnut::DummyPeer(
                                       *this->doughnut(), address)));
                  }
                });
            reactor::wait(scope);
          };
        });
    }

    void
    Configuration::serialize(elle::serialization::Serializer& s)
    {}
  }
}
