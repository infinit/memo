#ifndef INFINIT_MODEL_DOUGHNUT_DOCK
# define INFINIT_MODEL_DOUGHNUT_DOCK

# include <boost/asio.hpp>

# include <reactor/network/utp-socket.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/doughnut/protocol.hh>
# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Dock
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Dock(Doughnut& dht, Protocol protocol = Protocol::all);
        Dock(Dock const&) = delete;
        Dock(Dock&&);
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut);
        ELLE_ATTRIBUTE_R(std::vector<boost::asio::ip::address>,
                         local_ips);
        ELLE_ATTRIBUTE_R(Protocol, protocol);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::UTPServer>,
                       local_utp_server);
        ELLE_ATTRIBUTE(reactor::network::UTPServer&, utp_server);

      /*-----.
      | Peer |
      `-----*/
      public:
        overlay::Overlay::WeakMember
        make_peer(NodeLocation peer,
                  boost::optional<EndpointsRefetcher> refetcher);
        typedef
          std::unordered_map<Address, overlay::Overlay::WeakMember> PeerCache;
        ELLE_ATTRIBUTE(PeerCache, peer_cache);
      };
    }
  }
}

#endif
