#include <infinit/overlay/Stonehenge.hh>

#include <iterator>

#include <elle/Error.hh>
#include <elle/assert.hh>
#include <elle/log.hh>
#include <elle/utils.hh>

#include <das/serializer.hh>

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

    Stonehenge::Stonehenge(model::Address node_id, Peers peers,
                           model::doughnut::Doughnut* doughnut)
      : Overlay(std::move(node_id))
      , _peers(std::move(peers))
    {
      if (this->_peers.empty())
        throw elle::Error("empty peer list");
      this->doughnut(doughnut);
    }

    /*-------.
    | Lookup |
    `-------*/

    reactor::Generator<Overlay::Member>
    Stonehenge::_lookup(model::Address address,
                        int n,
                        Operation) const
    {
      // Use modulo on the address to determine the owner and yield the n
      // following nodes.
      return reactor::generator<Overlay::Member>( [this, address, n]
        (reactor::Generator<Overlay::Member>::yielder const& yield)
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
          yield(
            Overlay::Member(
              new infinit::model::doughnut::consensus::Paxos::RemotePeer(
                elle::unconst(*this->doughnut()),
                this->_peers[i].second,
                this->_peers[i].first)));
          i = (i + 1) % size;
        }
        while (i != (owner + n) % size);
      });
    }

    Overlay::Member
    Stonehenge::_lookup_node(model::Address address)
    {
      for (auto const& peer: this->_peers)
        if (peer.second == address)
          return Overlay::Member(
              new infinit::model::doughnut::consensus::Paxos::RemotePeer(
                elle::unconst(*this->doughnut()), peer.second, peer.first));
      ELLE_WARN("%s: could not find peer %s", *this, address);
      return nullptr;
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
    StonehengeConfiguration::make(
      NodeEndpoints const&, bool, model::doughnut::Doughnut* dht)
    {
      Stonehenge::Peers peers;
      for (auto const& peer: this->peers)
      {
        peers.emplace_back(
          Stonehenge::Host(
            boost::asio::ip::address::from_string(peer.host), peer.port),
          peer.id);
      }
      return elle::make_unique<infinit::overlay::Stonehenge>(
        this->node_id(), peers, dht);
    }

    static const elle::serialization::Hierarchy<Configuration>::
    Register<StonehengeConfiguration> _registerStonehengeConfiguration("stonehenge");
  }
}
