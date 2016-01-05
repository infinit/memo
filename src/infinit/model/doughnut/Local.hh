#ifndef INFINIT_MODEL_DOUGHNUT_LOCAL_HH
# define INFINIT_MODEL_DOUGHNUT_LOCAL_HH

# include <boost/signals2.hpp>

# include <reactor/Barrier.hh>
# include <reactor/network/tcp-server.hh>
# include <reactor/network/utp-socket.hh>

# include <infinit/RPC.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/model/doughnut/fwd.hh>
# include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Local
        : public Peer
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef Local Self;
        typedef Peer Super;
        enum class Protocol
        {
          tcp = 1,
          utp = 2,
          all = 3
        };

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
        ELLE_ATTRIBUTE_R(std::unique_ptr<storage::Storage>, storage);
        ELLE_ATTRIBUTE_R(Doughnut&, doughnut);

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
        boost::signals2::signal<
          void (blocks::Block const& block, StoreMode mode)> on_store;
        boost::signals2::signal<
          void (Address, std::unique_ptr<blocks::Block>&)> on_fetch;
        boost::signals2::signal<
          void (Address)> on_remove;
        boost::signals2::signal<
          void (RPCServer&)> on_connect;
      protected:
        virtual
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override;

      /*-------.
      | Server |
      `-------*/
      public:
        reactor::network::TCPServer::EndPoint
        server_endpoint();
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
        _require_auth(RPCServer& rpcs);

      /*----------.
      | Printable |
      `----------*/
      public:
        /// Print pretty representation to \a stream.
        virtual
        void
        print(std::ostream& stream) const override;
      };
    }
  }
}

namespace elle
{
  namespace serialization
  {
    template<> struct Serialize<infinit::model::doughnut::Local::Protocol>
    {
      typedef std::string Type;
      static
      std::string
      convert(infinit::model::doughnut::Local::Protocol p);
      static
      infinit::model::doughnut::Local::Protocol
      convert(std::string const& repr);
    };
  }
}
#endif
