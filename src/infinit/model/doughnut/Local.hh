#ifndef INFINIT_MODEL_DOUGHNUT_LOCAL_HH
# define INFINIT_MODEL_DOUGHNUT_LOCAL_HH

# include <reactor/network/tcp-server.hh>

# include <infinit/model/doughnut/Peer.hh>
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
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Local(std::unique_ptr<storage::Storage> storage);
        ~Local();
        ELLE_ATTRIBUTE_R(std::unique_ptr<storage::Storage>, storage);

      /*-------.
      | Blocks |
      `-------*/
      public:
        virtual
        void
        store(blocks::Block& block) override;
        virtual
        std::unique_ptr<blocks::Block>
        fetch(Address address) const override;
        virtual
        void
        remove(Address address) override;

      /*-------.
      | Server |
      `-------*/
      public:
        reactor::network::TCPServer::EndPoint
        server_endpoint();
        ELLE_ATTRIBUTE(reactor::network::TCPServer, server);
        ELLE_ATTRIBUTE(reactor::Thread, server_thread);
        void
        _serve();
      };
    }
  }
}

#endif
