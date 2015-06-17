#include <infinit/model/doughnut/Local.hh>

#include <elle/log.hh>
#include <elle/utility/Move.hh>

#include <reactor/Scope.hh>

#include <infinit/RPC.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/blocks/MutableBlock.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Local");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      Local::Local(std::unique_ptr<storage::Storage> storage, int port)
        : _storage(std::move(storage))
        , _server_thread(elle::sprintf("%s server", *this),
                         [this] { this->_serve(); })
      {
        this->_server.listen(port);
        ELLE_TRACE("%s: listen on port %s", *this, this->_server.port());
      }

      Local::~Local()
      {
        this->_server_thread.terminate_now();
      }

      /*-------.
      | Blocks |
      `-------*/

      void
      Local::store(blocks::Block const& block)
      {
        this->_storage->set(block.address(), block.data(), true, true);
      }

      std::unique_ptr<blocks::Block>
      Local::fetch(Address address) const
      {
        return Doughnut::_construct_block<blocks::MutableBlock>
          (address, this->_storage->get(address));
      }

      void
      Local::remove(Address address)
      {
        this->_storage->erase(address);
      }

      /*-------.
      | Server |
      `-------*/

      reactor::network::TCPServer::EndPoint
      Local::server_endpoint()
      {
        return this->_server.local_endpoint();
      }

      void
      Local::_serve()
      {
        RPCServer rpcs;
        rpcs.add("store",
                 std::function<void (blocks::Block const& data)>(
                   [this] (blocks::Block const& block)
                   {
                     return this->store(block);
                   }));
        rpcs.add("fetch",
                std::function<std::unique_ptr<blocks::Block> (Address address)>(
                  [this] (Address address)
                  {
                    return this->fetch(address);
                  }));
        rpcs.add("remove",
                std::function<void (Address address)>(
                  [this] (Address address)
                  {
                    this->remove(address);
                  }));
        elle::With<reactor::Scope>() << [this, &rpcs] (reactor::Scope& scope)
        {
          while (true)
          {
            auto socket = elle::utility::move_on_copy(this->_server.accept());
            auto name = elle::sprintf("%s: %s server", *this, **socket);
            scope.run_background(
              name,
              [this, socket, &rpcs]
              {
                rpcs.serve(**socket);
              });
          }
        };
      }
    }
  }
}
