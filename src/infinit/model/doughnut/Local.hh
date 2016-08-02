#ifndef INFINIT_MODEL_DOUGHNUT_LOCAL_HH
# define INFINIT_MODEL_DOUGHNUT_LOCAL_HH

# include <boost/signals2.hpp>

# include <reactor/Barrier.hh>
# include <reactor/network/tcp-server.hh>
# include <reactor/network/utp-socket.hh>

# include <infinit/RPC.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/protocol.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Local
        : virtual public Peer
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef Local Self;
        typedef Peer Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        Local(Doughnut& dht,
              Address id,
              std::unique_ptr<storage::Storage> storage,
              int port = 0,
              Protocol p = Protocol::all);
        ~Local();
        /** Called after every element of the DHT has been initialized.
         *
         *  The overlay does not exist upon construction, for instance.
         */
        virtual
        void
        initialize();
        /** Get ready for destrucion.
         *
         *  Release all shared self-reference.
         */
        virtual
        void
        cleanup();
        ELLE_ATTRIBUTE_R(std::unique_ptr<storage::Storage>, storage);

      /*-----------.
      | Networking |
      `-----------*/
      public:
        virtual
        void
        connect(elle::DurationOpt timeout) override;
        virtual
        void
        reconnect(elle::DurationOpt timeout) override;

      /*-------.
      | Blocks |
      `-------*/
      public:
        virtual
        void
        store(blocks::Block const& block, StoreMode mode) override;
        virtual
        void
        remove(Address address, blocks::RemoveSignature rs) override;
      protected:
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override;

      /*------.
      | Hooks |
      `------*/
      public:
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<void (blocks::Block const& block)>, on_store);
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<
            void (Address, std::unique_ptr<blocks::Block>&)>, on_fetch);
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<void (Address)>, on_remove);
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<void (RPCServer&)>, on_connect);

      /*-------.
      | Server |
      `-------*/
      public:
        reactor::network::TCPServer::EndPoint
        server_endpoint();
        std::vector<reactor::network::TCPServer::EndPoint>
        server_endpoints();
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::TCPServer>, server);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, server_thread);
        ELLE_ATTRIBUTE_RX(std::unique_ptr<reactor::network::UTPServer>, utp_server);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::Thread>, utp_server_thread);
        ELLE_ATTRIBUTE(reactor::Barrier, server_barrier);
      protected:
        virtual
        void
        _register_rpcs(RPCServer& rpcs);
        void
        _serve(std::function<std::unique_ptr<std::iostream> ()>);
        void
        _serve_tcp();
        void
        _serve_utp();
        void
        _require_auth(RPCServer& rpcs, bool write_op);
        std::unordered_map<RPCServer*, Passport> _passports;
      };
    }
  }
}

#endif
