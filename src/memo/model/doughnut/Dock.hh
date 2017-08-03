#pragma once

#include <elle/reactor/asio.hh>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index_container.hpp>

#include <elle/reactor/network/utp-socket.hh>

#include <memo/RPC.hh>
#include <memo/model/Address.hh>
#include <memo/model/doughnut/KeyCache.hh>
#include <memo/model/doughnut/Peer.hh>
#include <memo/model/doughnut/protocol.hh>
#include <memo/overlay/Overlay.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      namespace bmi = boost::multi_index;
      class Dock
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        class Connection;
        Dock(Doughnut& dht,
             Protocol protocol = {},
             boost::optional<int> port = {},
             boost::optional<boost::asio::ip::address> listen_address = {},
             boost::optional<std::string> rdv_host = {},
             elle::DurationOpt tcp_heartbeat = {});
        Dock(Dock const&) = delete;
        Dock(Dock&&);
        ~Dock();
        void
        disconnect();
        void
        cleanup();
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut);
        ELLE_ATTRIBUTE_R(elle::DurationOpt, tcp_heartbeat);
        ELLE_ATTRIBUTE(std::unique_ptr<elle::reactor::network::UTPServer>,
                       local_utp_server);
        ELLE_ATTRIBUTE_R(elle::reactor::network::UTPServer&, utp_server);
        ELLE_ATTRIBUTE(std::unique_ptr<elle::reactor::Thread>, rdv_connect_thread);
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<void (Connection&)>, on_connection);
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<void (std::shared_ptr<Remote>)>, on_peer);
        template <typename T, typename R, R (T::*M)() const>
        static
        R
        weak_access(std::weak_ptr<T> const& p)
        {
          return (ELLE_ENFORCE(p.lock()).get()->*M)();
        }

      /*-----------.
      | Connection |
      `-----------*/
      public:
        /// Connecting connections by endpoints.
        template <typename Connection>
        using Connecting = bmi::multi_index_container<
          std::weak_ptr<Connection>,
          bmi::indexed_by<
            bmi::hashed_non_unique<
              bmi::global_fun<
                std::weak_ptr<Connection> const&,
                Endpoints const&,
                &weak_access<
                  Connection, Endpoints const&, &Connection::endpoints>>>>>;
        /// Connected connections by peer id.
        template <typename Connection>
        using Connected = bmi::multi_index_container<
          std::weak_ptr<Connection>,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::global_fun<
                std::weak_ptr<Connection> const&,
                Address const&,
                &weak_access<Connection, Address const&, &Connection::id>>>>>;
        class Connection
          : public elle::Printable::as<Connection>
        {
        private:
          Connection(Dock& dock, NodeLocation loc);
        public:
          static
          std::shared_ptr<Connection>
          make(Dock& dock, NodeLocation loc);
          std::shared_ptr<Connection>
          shared_from_this();
          // Workaround shared_from_this in the constructor. Always build
          // connections through Dock::connect.
          void
          init();
          ~Connection() noexcept(false);
          ELLE_ATTRIBUTE_R(Dock&, dock);
          ELLE_ATTRIBUTE_R(NodeLocation, location);
          ELLE_attribute_r(Address, id);
          ELLE_attribute_r(Endpoints, endpoints);
          ELLE_ATTRIBUTE(std::unique_ptr<std::iostream>, socket);
          ELLE_ATTRIBUTE(std::unique_ptr<elle::protocol::Serializer>, serializer);
          ELLE_ATTRIBUTE_R(std::unique_ptr<elle::protocol::ChanneledStream>,
                           channels, protected);
          ELLE_ATTRIBUTE_RX(RPCServer, rpc_server);
          ELLE_ATTRIBUTE_R(elle::Buffer, credentials, protected);
          ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr, thread);
          /// Whether the remote has ever connected.
          ELLE_ATTRIBUTE_R(bool, connected);
          ELLE_ATTRIBUTE_R(boost::optional<model::Endpoint>,
                           connected_endpoint);
          /// Whether the remote has disconnected or won't ever connect.
          ELLE_ATTRIBUTE_R(bool, disconnected);
          ELLE_ATTRIBUTE_RX(boost::signals2::signal<void ()>, on_connection);
          ELLE_ATTRIBUTE_RX(boost::signals2::signal<void ()>, on_disconnection);
          ELLE_ATTRIBUTE_R(elle::Time, disconnected_since);
          ELLE_ATTRIBUTE_R(std::exception_ptr, disconnected_exception);
          ELLE_ATTRIBUTE_RX(KeyCache, key_hash_cache);
          ELLE_ATTRIBUTE(std::function<void()>, cleanup_on_disconnect);
          ELLE_ATTRIBUTE_R(std::weak_ptr<Connection>, self);
          ELLE_ATTRIBUTE(boost::optional<Connected<Connection>::iterator>,
                         connected_it);

        public:
          void
          disconnect();
          void
          print(std::ostream& out) const;
        private:
          void
          _key_exchange(elle::protocol::ChanneledStream& channels);
          friend class Dock;
        };

        /// Get a connection to the given location.
        ///
        /// Retreive a connection to the current location, either already
        /// connected or currently connecting, or open a new one if there is
        /// none. Ownership is not kept by the Dock. If the location id is null,
        /// fill it after connection. If it is set, disconnect in case of
        /// mismatch.
        ///
        /// @param l         Location of the peer to connect to.
        /// @param no_remote Do not automatically create a remote on this conneciton
        std::shared_ptr<Connection>
        connect(NodeLocation l, bool no_remote = false);
        ELLE_ATTRIBUTE_R(Connecting<Connection>, connecting);
        ELLE_ATTRIBUTE(Connected<Connection>, connected);

      /*-----.
      | Peer |
      `-----*/
      public:
        overlay::Overlay::WeakMember
        make_peer(NodeLocation peer);
        std::shared_ptr<Remote>
        make_peer(std::shared_ptr<Connection> connection, bool ignored_result = false);
        /// Weak references to all remotes. They will never be null as they
        /// unregister themselves on destruction.
        using PeerCache = bmi::multi_index_container<
          std::weak_ptr<Peer>,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::global_fun<std::weak_ptr<Peer> const&,
                              Address const&,
                              &weak_access<Peer, Address const&, &Peer::id>>>>>;
        ELLE_ATTRIBUTE_R(PeerCache, peer_cache);
        friend class Remote;
      };
    }
  }
}
