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
          Auth(elle::serialization::SerializerIn& input);
          Address id;
          Challenge challenge;
          Passport passport;
        };

      /*-------------.
      | Construction |
      `-------------*/
      public:
        Remote(Doughnut& doughnut,
               Address id,
               Endpoints endpoints,
               boost::optional<reactor::network::UTPServer&> server,
               boost::optional<EndpointsRefetcher> const& refetch = {},
               Protocol protocol = Protocol::all);
        virtual
        ~Remote();
      protected:
        ELLE_ATTRIBUTE(std::unique_ptr<std::iostream>, socket);
        ELLE_ATTRIBUTE(std::unique_ptr<protocol::Serializer>, serializer);
        ELLE_ATTRIBUTE_R(std::unique_ptr<protocol::ChanneledStream>,
                         channels, protected);

      /*-----------.
      | Connection |
      `-----------*/
      public:
        virtual
        void
        connect(elle::DurationOpt timeout = elle::DurationOpt()) override;
        virtual
        void
        reconnect(elle::DurationOpt timeout = elle::DurationOpt()) override;
      private:
        void
        _connect();
        ELLE_ATTRIBUTE(bool, connected);
        ELLE_ATTRIBUTE(bool, reconnecting);
        ELLE_ATTRIBUTE_R(int, reconnection_id);
        ELLE_ATTRIBUTE_R(Endpoints, endpoints);
        ELLE_ATTRIBUTE(boost::optional<reactor::network::UTPServer&>,
                       utp_server);

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
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, connection_thread);
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
      };

      template<typename F>
      class RemoteRPC
        : public RPC<F>
      {
      public:
        typedef RPC<F> Super;
        RemoteRPC(std::string name, Remote* remote)
          : Super(name, *remote->channels(),
                  remote->doughnut().version(),
                  elle::unconst(&remote->credentials()))
          , _remote(remote)
        {}
        template<typename ...Args>
        typename Super::result_type
        operator()(Args const& ... args);
        Remote* _remote;
      };
    }
  }
}

#include <infinit/model/doughnut/Remote.hxx>

#endif
