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
    /*----------.
    | Operation |
    `----------*/

    std::ostream&
    operator <<(std::ostream& output, Operation op)
    {
      switch (op)
      {
        case OP_FETCH:
          output << "fetch";
          break;
        case OP_INSERT:
          output << "insert";
          break;
        case OP_UPDATE:
          output << "update";
          break;
        case OP_REMOVE:
          output << "remove";
          break;
        case OP_FETCH_FAST:
          output << "fetch_fast";
          break;
      }
      return output;
    }

    /*-------------.
    | Construction |
    `-------------*/

    Overlay::Overlay(model::doughnut::Doughnut* dht,
                     std::shared_ptr<infinit::model::doughnut::Local> local)
      : _doughnut(dht)
      , _local(local)
    {}

    Overlay::~Overlay()
    {
      ELLE_TRACE("%s: destruct", this);
    }

    model::Address const&
    Overlay::id() const
    {
      return this->_doughnut->id();
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
    Overlay::discover(Endpoints const& peer)
    {
      this->discover(NodeLocation(model::Address::null, peer));
    }

    void
    Overlay::discover(std::vector<Endpoints> const& peers)
    {
      NodeLocations locs;
      for (auto const& eps: peers)
        locs.emplace_back(model::Address::null, eps);
      this->discover(std::move(locs));
    }

    void
    Overlay::discover(NodeLocation const& peer)
    {
      this->discover(NodeLocations{peer});
    }

    void
    Overlay::discover(NodeLocations const& peers_)
    {
      ELLE_TRACE("%s: discover %f", this, peers_);
      NodeLocations peers(peers_);
      auto it = std::remove_if(peers.begin(), peers.end(),
        [this] (NodeLocation const& nl)
        {
          bool is_us = (nl.id() == this->doughnut()->id());
          if (is_us)
            ELLE_TRACE("%s: removeing ourself from peer list", this);
          return is_us;
        });
      peers.erase(it, peers.end());
      this->_discover(peers);
    }

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<std::pair<model::Address, Overlay::WeakMember>>
    Overlay::lookup(std::vector<model::Address> const& addresses, int n) const
    {
      ELLE_TRACE_SCOPE("%s: lookup %s nodes for %f", *this, n, addresses);
      return this->_lookup(addresses, n);
    }

    reactor::Generator<Overlay::WeakMember>
    Overlay::lookup(model::Address address, int n, Operation op) const
    {
      ELLE_TRACE_SCOPE("%s: lookup %s nodes for %f (%s)", this, n, address, op);
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
    Overlay::lookup_node(model::Address address) const
    {
      if (auto res = this->_lookup_node(address))
        return res;
      else
        throw NodeNotFound(address);
    }

    reactor::Generator<Overlay::WeakMember>
    Overlay::lookup_nodes(std::unordered_set<model::Address> addresses) const
    {
      ELLE_TRACE_SCOPE("%s: lookup nodes %f", this, addresses);
      return reactor::generator<Overlay::WeakMember>(
        [this, addresses]
        (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
        {
          reactor::for_each_parallel(
            addresses,
            [&] (model::Address const& address)
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
            },
            "fetch node by address");
        });
    }

    void
    Configuration::serialize(elle::serialization::Serializer& s)
    {}

    /*-----------.
    | Monitoring |
    `-----------*/

    std::vector<std::pair<std::string, std::string>>
    Overlay::peer_list()
    {
      return {};
    }

    elle::json::Object
    Overlay::information()
    {
      return {};
    }

    /*-----------.
    | Exceptions |
    `-----------*/

    NodeNotFound::NodeNotFound(model::Address id)
      : elle::Error(elle::sprintf("node not found: %f", id))
      , _id(id)
    {}
  }
}
