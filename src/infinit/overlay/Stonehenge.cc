#include <infinit/overlay/Stonehenge.hh>

#include <iterator>

#include <elle/Error.hh>
#include <elle/assert.hh>
#include <elle/log.hh>
#include <elle/utils.hh>

#include <das/serializer.hh>

#include <reactor/network/resolve.hh>

#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh> // FIXME

ELLE_LOG_COMPONENT("infinit.overlay.Stonehenge");

DAS_MODEL(infinit::overlay::StonehengeConfiguration::Peer,
          (host, port, id), DasPeer);
DAS_MODEL_DEFAULT(infinit::overlay::StonehengeConfiguration::Peer, DasPeer);
DAS_MODEL_SERIALIZE(infinit::overlay::StonehengeConfiguration::Peer);

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Stonehenge::Peer::Peer(model::Address id_)
      : id(std::move(id_))
      , endpoint()
    {}

    Stonehenge::Peer::Peer(model::Address id_, Endpoint e)
      : id(std::move(id_))
      , endpoint(std::move(e))
    {}

    Stonehenge::Stonehenge(Peers peers,
                           std::shared_ptr<model::doughnut::Local> local,
                           model::doughnut::Doughnut* doughnut)
      : Overlay(doughnut, std::move(local))
      , _peers(std::move(peers))
    {
      if (this->_peers.empty())
        throw elle::Error("empty peer list");
    }

    /*------.
    | Peers |
    `------*/

    void
    Stonehenge::_discover(NodeLocations const& peers)
    {
      elle::err("Stonehenge cannot discover new nodes");
    }

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<Overlay::WeakMember>
    Stonehenge::_lookup(model::Address address,
                        int n,
                        Operation) const
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
    Stonehenge::_lookup_node(model::Address address)
    {
      for (auto const& peer: this->_peers)
        if (peer.id == address)
          return this->_make_member(peer);
      ELLE_WARN("%s: could not find peer %s", *this, address);
      return Overlay::Member(nullptr);
    }

    Overlay::WeakMember
    Stonehenge::_make_member(Peer const& peer) const
    {
      if (!peer.endpoint)
        throw elle::Error(elle::sprintf("missing endpoint for %s", peer.id));
      return Overlay::WeakMember(
        new infinit::model::doughnut::consensus::Paxos::RemotePeer(
          elle::unconst(*this->doughnut()),
          peer.id, reactor::network::resolve_tcp(
            peer.endpoint->host, std::to_string(peer.endpoint->port))));
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
    StonehengeConfiguration::make(std::vector<Endpoints> const&,
                                  std::shared_ptr<model::doughnut::Local> local,
                                  model::doughnut::Doughnut* dht)
    {
      Stonehenge::Peers peers;
      for (auto const& peer: this->peers)
      {
        peers.emplace_back(
          peer.id,
          Stonehenge::Peer::Endpoint{peer.host, peer.port});
      }
      return elle::make_unique<infinit::overlay::Stonehenge>(
        peers, std::move(local), dht);
    }

    static const elle::serialization::Hierarchy<Configuration>::
    Register<StonehengeConfiguration> _registerStonehengeConfiguration("stonehenge");
  }
}
