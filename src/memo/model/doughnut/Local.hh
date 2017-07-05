#pragma once

#include <tuple>

#include <boost/signals2.hpp>

#include <elle/reactor/Barrier.hh>
#include <elle/reactor/network/tcp-server.hh>
#include <elle/reactor/network/utp-socket.hh>

#include <memo/RPC.hh>
#include <memo/model/doughnut/Peer.hh>
#include <memo/model/doughnut/fwd.hh>
#include <memo/model/doughnut/protocol.hh>
#include <memo/silo/Silo.hh>

namespace memo
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
        using Self = Local;
        using Super = Peer;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        Local(Doughnut& dht,
              Address id,
              std::unique_ptr<silo::Silo> storage,
              int port = 0,
              boost::optional<boost::asio::ip::address> listen_address = {});
        ~Local() override;
        /** Called after every element of the DHT has been initialized.
         *
         *  The overlay does not exist upon construction, for instance.
         */
        virtual
        void
        initialize();
        ELLE_ATTRIBUTE_R(std::unique_ptr<silo::Silo>, storage);
        ELLE_attribute_r(elle::Version, version);
      protected:
        void
        _cleanup() override;

      /*-------.
      | Blocks |
      `-------*/
      public:
        void
        store(blocks::Block const& block, StoreMode mode) override;
        void
        remove(Address address, blocks::RemoveSignature rs) override;
      protected:
        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override;

      /*-----.
      | Keys |
      `-----*/
      protected:
        std::vector<elle::cryptography::rsa::PublicKey>
        _resolve_keys(std::vector<int> const& ids) override;
        std::unordered_map<int, elle::cryptography::rsa::PublicKey>
        _resolve_all_keys() override;

      /*----.
      | RPC |
      `----*/
      public:
        template <typename R, typename ... Args>
        R
        broadcast(std::string const& name, Args&& ...);

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
        Endpoint
        server_endpoint();
        Endpoints
        server_endpoints();
        class Connection
          : public elle::Printable
        {
        public:
          Connection(Local& local, std::shared_ptr<std::iostream> stream);

          void
          print(std::ostream&) const override;

        private:
          friend class doughnut::Local;
          void
          _run();
          ELLE_ATTRIBUTE_R(Local&, local);
          ELLE_ATTRIBUTE_R(std::shared_ptr<std::iostream>, stream);
          ELLE_ATTRIBUTE_R(elle::protocol::Serializer, serializer);
          ELLE_ATTRIBUTE_R(elle::protocol::ChanneledStream, channels);
          ELLE_ATTRIBUTE_RX(RPCServer, rpcs);
          ELLE_ATTRIBUTE_R(Address, id);
          ELLE_ATTRIBUTE_RX(boost::signals2::signal<void()>, ready);
        };
        ELLE_ATTRIBUTE(std::unique_ptr<elle::reactor::network::TCPServer>, server);
        ELLE_ATTRIBUTE(std::unique_ptr<elle::reactor::Thread>, server_thread);
        ELLE_ATTRIBUTE_RX(std::unique_ptr<elle::reactor::network::UTPServer>, utp_server);
        ELLE_ATTRIBUTE(std::unique_ptr<elle::reactor::Thread>, utp_server_thread);
        ELLE_ATTRIBUTE(elle::reactor::Barrier, server_barrier);
        ELLE_ATTRIBUTE_R(std::list<std::shared_ptr<Connection>>, peers);
      protected:
        virtual
        void
        _register_rpcs(Connection& rpcs);
        void
        _serve(std::function<std::unique_ptr<std::iostream> ()>);
        void
        _serve_tcp();
        void
        _serve_utp();
        void
        _require_auth(RPCServer& rpcs, bool write_op);
        std::unordered_map<RPCServer*, Passport> _passports;

      /*----------.
      | Printable |
      `----------*/
      public:
        /// Print pretty representation to \a stream.
        void
        print(std::ostream& stream) const override;
      };
    }
  }
}

#include <memo/model/doughnut/Local.hxx>
