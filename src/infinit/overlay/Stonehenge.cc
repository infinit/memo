#include <infinit/overlay/Stonehenge.hh>

#include <iterator>

#include <elle/Error.hh>
#include <elle/assert.hh>
#include <elle/log.hh>
#include <elle/utils.hh>

#include <das/serializer.hh>

#include <reactor/network/resolve.hh>
#include <reactor/network/utp-server.hh>

#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh> // FIXME

ELLE_LOG_COMPONENT("infinit.overlay.Stonehenge");

DAS_SERIALIZE(infinit::overlay::StonehengeConfiguration::Peer);

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Stonehenge::Stonehenge(NodeLocations peers,
                           std::shared_ptr<model::doughnut::Local> local,
                           model::doughnut::Doughnut* doughnut)
      : Overlay(doughnut, std::move(local))
      , _peers(std::move(peers))
    {}

    /*------.
    | Peers |
    `------*/

    void
    Stonehenge::_discover(NodeLocations const& peers)
    {
      elle::err("Stonehenge cannot discover new nodes");
    }

    bool
    Stonehenge::_discovered(model::Address id)
    {
      return any(this->_peers,
                 [&] (NodeLocation const& p) { return p.id() == id; });
    }

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<Overlay::WeakMember>
    Stonehenge::_allocate(model::Address address, int n) const
    {
      return this->_lookup(address, n, false);
    }

    reactor::Generator<Overlay::WeakMember>
    Stonehenge::_lookup(model::Address address, int n, bool) const
    {
      // Use modulo on the address to determine the owner and yield the n
      // following nodes.
      return reactor::generator<Overlay::WeakMember>(
        [this, address, n]
        (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
        {
          int size = this->_peers.size();
          ELLE_ASSERT_LTE(n, size);
          auto owner = address.value()[0] % size;
          ELLE_ASSERT(this->doughnut());
          int i = owner;
          do
          {
            ELLE_DEBUG("%s: yield %s", *this, this->_peers[i]);
            // FIXME: don't always yield Paxos
            yield(this->_make_member(this->_peers[i]));
            i = (i + 1) % size;
          }
          while (i != (owner + n) % size);
        });
    }

    Overlay::WeakMember
    Stonehenge::_lookup_node(model::Address address) const
    {
      for (auto const& peer: this->_peers)
        if (peer.id() == address)
          return this->_make_member(peer);
      ELLE_WARN("%s: could not find peer %s", *this, address);
      return Overlay::Member(nullptr);
    }

    Overlay::WeakMember
    Stonehenge::_make_member(NodeLocation const& peer) const
    {
      if (peer.endpoints().empty())
        throw elle::Error(elle::sprintf("missing endpoint for %f", peer.id()));
      return this->doughnut()->dock().make_peer(peer);
    }

    /*-----------.
    | Monitoring |
    `-----------*/

    std::string
    Stonehenge::type_name()
    {
      return "stonehenge";
    }

    elle::json::Array
    Stonehenge::peer_list()
    {
      elle::json::Array res;
      for (auto const& peer: this->_peers)
        res.push_back(elle::json::Object{
          { "id", elle::sprintf("%x", peer.id()) },
          { "endpoints", elle::sprintf("%s", peer.endpoints()) }
        });
      return res;
    }

    elle::json::Object
    Stonehenge::stats()
    {
      elle::json::Object res;
      res["type"] = this->type_name();
      return res;
    }

    StonehengeConfiguration::StonehengeConfiguration()
      : overlay::Configuration()
    {}

    StonehengeConfiguration::StonehengeConfiguration
      (elle::serialization::SerializerIn& input)
      : Configuration(input)
    {
      this->serialize(input);
    }

    void
    StonehengeConfiguration::serialize(elle::serialization::Serializer& s)
    {
      Configuration::serialize(s);
      s.serialize("peers", this->peers);
    }

    std::unique_ptr<infinit::overlay::Overlay>
    StonehengeConfiguration::make(std::shared_ptr<model::doughnut::Local> local,
                                  model::doughnut::Doughnut* dht)
    {
      NodeLocations peers;
      for (auto const& peer: this->peers)
      {
        peers.emplace_back(
          peer.id,
          Endpoints({model::Endpoint(peer.host, peer.port)}));
      }
      return elle::make_unique<infinit::overlay::Stonehenge>(
        peers, std::move(local), dht);
    }

    static const elle::serialization::Hierarchy<Configuration>::
    Register<StonehengeConfiguration> _registerStonehengeConfiguration("stonehenge");
  }
}
