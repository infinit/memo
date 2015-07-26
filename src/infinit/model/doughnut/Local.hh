#ifndef INFINIT_MODEL_DOUGHNUT_LOCAL_HH
# define INFINIT_MODEL_DOUGHNUT_LOCAL_HH

# include <boost/signals2.hpp>

# include <reactor/network/tcp-server.hh>

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
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Local(std::unique_ptr<storage::Storage> storage, int port = 0);
        ~Local();
        ELLE_ATTRIBUTE_R(std::unique_ptr<storage::Storage>, storage);
        ELLE_ATTRIBUTE_RX(std::unique_ptr<Doughnut>, doughnut);

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

        boost::signals2::signal<void (blocks::Block const& block, StoreMode mode)> on_store;
        boost::signals2::signal<void (Address, std::unique_ptr<blocks::Block>&)> on_fetch;
        boost::signals2::signal<void (Address)> on_remove;

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
