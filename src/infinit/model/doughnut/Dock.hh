#ifndef INFINIT_MODEL_DOUGHNUT_DOCK
# define INFINIT_MODEL_DOUGHNUT_DOCK

# include <boost/asio.hpp>

# include <reactor/network/utp-socket.hh>

# include <infinit/RPC.hh>
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
        Dock(Doughnut& dht,
             Protocol protocol = Protocol::all,
             boost::optional<std::string> rdv_host = {});
        Dock(Dock const&) = delete;
        Dock(Dock&&);
        ~Dock();
        void cleanup();
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut);
        ELLE_ATTRIBUTE_R(std::vector<boost::asio::ip::address>,
                         local_ips);
        ELLE_ATTRIBUTE_R(Protocol, protocol);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::UTPServer>,
                       local_utp_server);
        ELLE_ATTRIBUTE_R(reactor::network::UTPServer&, utp_server);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, rdv_connect_thread);
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<void (Remote&)>,
          on_connect);

      /*-----.
      | Peer |
      `-----*/
      public:
        overlay::Overlay::WeakMember
        make_peer(NodeLocation peer,
                  boost::optional<EndpointsRefetcher> refetcher);
        using PeerCache = std::unordered_map<Address, overlay::Overlay::Member>;
        ELLE_ATTRIBUTE_R(PeerCache, peer_cache);
      };
    }
  }
}

#endif
