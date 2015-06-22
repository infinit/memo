#ifndef INFINIT_MODEL_DOUGHNUT_REMOTE_HH
# define INFINIT_MODEL_DOUGHNUT_REMOTE_HH

# include <reactor/network/tcp-socket.hh>

# include <protocol/Serializer.hh>
# include <protocol/ChanneledStream.hh>

# include <infinit/model/doughnut/Peer.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Remote
        : public Peer
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Remote(boost::asio::ip::tcp::endpoint endpoint);
        Remote(std::string const& host, int port);
        ELLE_ATTRIBUTE(reactor::network::TCPSocket, socket);
        ELLE_ATTRIBUTE(protocol::Serializer, serializer);
        ELLE_ATTRIBUTE(protocol::ChanneledStream, channels);

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
      };
    }
  }
}

#endif
