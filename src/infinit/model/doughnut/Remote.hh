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

      /*-------------.
      | Construction |
      `-------------*/
      public:
        Remote(Doughnut& doughnut, Address id,
               boost::asio::ip::tcp::endpoint endpoint);
        Remote(Doughnut& doughnut, Address id,
               boost::asio::ip::udp::endpoint endpoint,
               reactor::network::UTPServer& server);
        Remote(Doughnut& doughnut, Address id,
               std::vector<boost::asio::ip::udp::endpoint> endpoints,
               std::string const& peer_id,
               reactor::network::UTPServer& server);
        virtual
        ~Remote();
      protected:
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::TCPSocket>, socket);
        ELLE_ATTRIBUTE(std::unique_ptr<reactor::network::UTPSocket>, utp_socket);
        ELLE_ATTRIBUTE(std::unique_ptr<protocol::Serializer>, serializer);
        ELLE_ATTRIBUTE_R(std::unique_ptr<protocol::ChanneledStream>,
                         channels, protected);

      /*-----------.
      | Networking |
      `-----------*/
      public:
        virtual
        void
        connect(elle::DurationOpt timeout = elle::DurationOpt()) override;
        virtual
        void
        reconnect(elle::DurationOpt timeout = elle::DurationOpt()) override;
        void
        initiate_connect(std::vector<boost::asio::ip::udp::endpoint> endpoints,
                         std::string const& peer_id,
                         reactor::network::UTPServer& server);
        void
        initiate_connect(boost::asio::ip::tcp::endpoint endpoint);
        template<typename F>
        RemoteRPC<F>
        make_rpc(std::string const& name);
        template<typename R>
        R
        safe_perform(std::string const& name, std::function<R()> op);
      private:
        void
        _connect(std::string endpoint,
                 std::function <std::iostream& ()> const& socket);
        void
        _key_exchange();
        ELLE_ATTRIBUTE(std::function <std::iostream& ()>, connector);
        ELLE_ATTRIBUTE(std::string, endpoint);
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, connection_thread);
        ELLE_ATTRIBUTE(reactor::Mutex, connection_mutex);
        ELLE_ATTRIBUTE(reactor::Mutex, reconnection_mutex);
        ELLE_ATTRIBUTE_R(elle::Buffer, credentials, protected);
        /* Callback is expected to retry an async connection, with
           potentially updated endpoints. Should return false if it
           did nothing.
        */
        ELLE_ATTRIBUTE_RW(std::function<bool (Remote&)>, retry_connect);
        ELLE_ATTRIBUTE_R(bool, fast_fail);
        // Do *not* use, call connect() instead.
        ELLE_ATTRIBUTE(bool, connected);
        ELLE_ATTRIBUTE_R(int, reconnection_id);
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

      template<typename F>
      RemoteRPC<F>
      Remote::make_rpc(std::string const& name)
      {
        return RemoteRPC<F>(name, this);
      }
    }
  }
}

#include <infinit/model/doughnut/Remote.hxx>

#endif
