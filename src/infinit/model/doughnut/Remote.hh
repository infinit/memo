#ifndef INFINIT_MODEL_DOUGHNUT_REMOTE_HH
# define INFINIT_MODEL_DOUGHNUT_REMOTE_HH

# include <reactor/network/tcp-socket.hh>
# include <reactor/network/utp-socket.hh>
# include <reactor/thread.hh>

# include <protocol/Serializer.hh>
# include <protocol/ChanneledStream.hh>

# include <infinit/model/doughnut/fwd.hh>
# include <infinit/model/doughnut/Peer.hh>

# include <infinit/RPC.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Remote
        : public Peer
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
               std::string const& host, int port);
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
        Doughnut& _doughnut;
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
        template<typename F>
        RPC<F>
        make_rpc(std::string const& name);
      private:
        void
        _connect(std::string endpoint,
                 std::function <std::iostream& ()> const& socket);
        void
        _key_exchange();
        ELLE_ATTRIBUTE(std::function <std::iostream& ()>, connector);
        ELLE_ATTRIBUTE(std::string, endpoint);
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, connection_thread);
        ELLE_ATTRIBUTE_R(elle::Buffer, credentials, protected);
      /*-------.
      | Blocks |
      `-------*/
      public:
        virtual
        void
        store(blocks::Block const& block, StoreMode mode) override;
        virtual
        std::unique_ptr<blocks::Block>
        fetch(Address address) const override;
        virtual
        void
        remove(Address address) override;

      /*----------.
      | Printable |
      `----------*/
      public:
        /// Print pretty representation to \a stream.
        virtual
        void
        print(std::ostream& stream) const override;
      };

      template<typename F>
      RPC<F>
      Remote::make_rpc(std::string const& name)
      {
        return RPC<F>(name, *this->_channels, &this->_credentials);
      }
    }
  }
}

#endif
