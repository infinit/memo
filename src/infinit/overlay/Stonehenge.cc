#include <infinit/overlay/Stonehenge.hh>

#include <iterator>

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/range/algorithm/find_if.hpp>

#include <elle/Error.hh>
#include <elle/assert.hh>
#include <elle/log.hh>
#include <elle/make-vector.hh>
#include <elle/utils.hh>

#include <elle/das/serializer.hh>

#include <elle/reactor/network/resolve.hh>
#include <elle/reactor/network/utp-server.hh>

#include <infinit/model/doughnut/Remote.hh>

ELLE_LOG_COMPONENT("infinit.overlay.Stonehenge");

ELLE_DAS_SERIALIZE(infinit::overlay::StonehengeConfiguration::Peer);

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
      return boost::algorithm::any_of(this->_peers,
                 [&] (NodeLocation const& p) { return p.id() == id; });
    }

    /*-------.
    | Lookup |
    `-------*/

    auto
    Stonehenge::_allocate(model::Address address, int n) const
      -> MemberGenerator
    {
      return this->_lookup(address, n, false);
    }

    auto
    Stonehenge::_lookup(model::Address address, int n, bool) const
      -> MemberGenerator
    {
      // Use modulo on the address to determine the owner and yield the n
      // following nodes.
      return [this, address, n](MemberGenerator::yielder const& yield)
        {
          int size = this->_peers.size();
          ELLE_ASSERT_LTE(n, size);
          auto owner = address.value()[0] % size;
          ELLE_ASSERT(this->doughnut());
          int i = owner;
          do
          {
            ELLE_DEBUG("%s: yield %s", *this, this->_peers[i]);
            yield(this->_make_member(this->_peers[i]));
            i = (i + 1) % size;
          }
          while (i != (owner + n) % size);
        };
    }

    auto
    Stonehenge::_lookup_node(model::Address address) const
      -> WeakMember
    {
      auto it = boost::find_if(this->_peers,
                               [&address](auto const& peer)
                               {
                                 return peer.id() == address;
                               });
      if (it == end(this->_peers))
      {
        ELLE_WARN("%s: could not find peer %s", *this, address);
        return Overlay::Member(nullptr);
      }
      else
        return this->_make_member(*it);
    }

    Overlay::WeakMember
    Stonehenge::_make_member(NodeLocation const& peer) const
    {
      if (peer.endpoints().empty())
        elle::err("missing endpoint for %f", peer.id());
      else
        return this->doughnut()->dock().make_peer(peer);
    }

    /*-----------.
    | Monitoring |
    `-----------*/

    std::string
    Stonehenge::type_name() const
    {
      return "stonehenge";
    }

    elle::json::Array
    Stonehenge::peer_list() const
    {
      auto res = elle::json::Array{};
      for (auto const& peer: this->_peers)
        res.push_back
          (elle::json::Object{
            { "id", elle::sprintf("%x", peer.id()) },
            { "endpoints", elle::sprintf("%s", peer.endpoints()) }
          });
      return res;
    }

    elle::json::Object
    Stonehenge::stats() const
    {
      return {{"type", this->type_name()}};
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
      auto peers =
        elle::make_vector(this->peers,
                          [](auto const& peer) -> NodeLocation
                          {
                            return
                              {
                                peer.id,
                                Endpoints(peer.host, peer.port)
                              };
                          });
      return std::make_unique<infinit::overlay::Stonehenge>(
        std::move(peers), std::move(local), dht);
    }

    namespace
    {
      auto const res =
        elle::serialization::Hierarchy<Configuration>
        ::Register<StonehengeConfiguration>("stonehenge");
    }
  }
}
