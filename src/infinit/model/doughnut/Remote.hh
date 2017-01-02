#ifndef INFINIT_MODEL_DOUGHNUT_REMOTE_HH
# define INFINIT_MODEL_DOUGHNUT_REMOTE_HH

# include <elle/utils.hh>

# include <reactor/network/tcp-socket.hh>
# include <reactor/network/utp-socket.hh>
# include <reactor/thread.hh>

# include <protocol/Serializer.hh>
# include <protocol/ChanneledStream.hh>

# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/Doughnut.hh>
# include <infinit/model/doughnut/Peer.hh>
# include <infinit/model/doughnut/protocol.hh>

# include <infinit/RPC.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      template<typename F>
      class RemoteRPC;

      class Remote
        : virtual public Peer
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef Remote Self;
        typedef Peer Super;
        /// Challenge, token
        using Challenge = std::pair<elle::Buffer, elle::Buffer>;
        struct Auth
        {
          Auth(Address id,
               Challenge challenge,
               Passport passport);
          Address id;
          Challenge challenge;
          Passport passport;
          using Model = das::Model<
            Auth,
            decltype(elle::meta::list(
                       symbols::id,
                       symbols::challenge,
                       symbols::passport))>;
        };

      /*-------------.
      | Construction |
      `-------------*/
      public:
        Remote(
          Doughnut& doughnut,
          Address id,
          Endpoints endpoints,
          boost::optional<reactor::network::UTPServer&> server = boost::none,
          boost::optional<EndpointsRefetcher> const& refetch = boost::none,
          Protocol protocol = Protocol::all);
        virtual
        ~Remote();
      protected:
        void
        _cleanup() override;
        ELLE_ATTRIBUTE(std::unique_ptr<std::iostream>, socket);
        ELLE_ATTRIBUTE(std::unique_ptr<protocol::Serializer>, serializer);
        ELLE_ATTRIBUTE_R(std::unique_ptr<protocol::ChanneledStream>,
                         channels, protected);
        ELLE_ATTRIBUTE_RX(RPCServer, rpc_server);

      /*-----------.
      | Connection |
      `-----------*/
      public:
        virtual
        void
        connect(elle::DurationOpt timeout = elle::DurationOpt());
        virtual
        void
        reconnect(elle::DurationOpt timeout = elle::DurationOpt());
      private:
        void
        _connect();
        ELLE_ATTRIBUTE(reactor::Barrier, connected);
        ELLE_ATTRIBUTE(bool, reconnecting);
        ELLE_ATTRIBUTE_R(int, reconnection_id);
        ELLE_ATTRIBUTE_R(Endpoints, endpoints);
        ELLE_ATTRIBUTE(boost::optional<reactor::network::UTPServer&>,
                       utp_server);
        ELLE_ATTRIBUTE_RX(boost::signals2::signal<void()>, id_discovered);

      /*-----------.
      | Networking |
      `-----------*/
      public:
        template<typename F>
        RemoteRPC<F>
        make_rpc(std::string const& name);
        template<typename R>
        R
        safe_perform(std::string const& name, std::function<R()> op);
      private:
        void
        _key_exchange(protocol::ChanneledStream& channels);
        ELLE_ATTRIBUTE(Protocol, protocol);
        ELLE_ATTRIBUTE_R(elle::Buffer, credentials, protected);
        ELLE_ATTRIBUTE_R(EndpointsRefetcher, refetch_endpoints);
        ELLE_ATTRIBUTE_R(bool, fast_fail);
        ELLE_ATTRIBUTE(std::chrono::system_clock::time_point, connection_start_time);
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, thread);
        ELLE_ATTRIBUTE(reactor::Mutex, connect_mutex);

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

      /*-----.
      | Keys |
      `-----*/
      protected:
        virtual
        std::vector<cryptography::rsa::PublicKey>
        _resolve_keys(std::vector<int> ids) override;
        virtual
        std::unordered_map<int, cryptography::rsa::PublicKey>
        _resolve_all_keys() override;
        ELLE_ATTRIBUTE_R(Doughnut::KeyCache, key_hash_cache);
      };

      template <typename F>
      class RemoteRPC
        : public RPC<F>
      {
      public:
        using Super = RPC<F>;
        RemoteRPC(std::string name, Remote* remote);
        template<typename ...Args>
        typename Super::result_type
        operator()(Args const& ... args);
        Remote* _remote;
      };
    }
  }
}

DAS_SERIALIZE(infinit::model::doughnut::Remote::Auth);

#include <infinit/model/doughnut/Remote.hxx>

#endif
