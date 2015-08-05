#include <infinit/model/doughnut/Local.hh>

#include <elle/log.hh>
#include <elle/utility/Move.hh>

#include <reactor/Scope.hh>

#include <infinit/RPC.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/Model.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Local");

typedef elle::serialization::Binary Serializer;

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
      Local::store(blocks::Block const& block, StoreMode mode)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        try
        {
          auto previous_buffer = this->_storage->get(block.address());
          elle::IOStream s(previous_buffer.istreambuf());
          typename elle::serialization::binary::SerializerIn input(s, false);
          input.set_context<Doughnut*>(this->_doughnut.get());
          auto previous = input.deserialize<std::unique_ptr<blocks::Block>>();
          if (!block.validate(*previous))
            throw ValidationFailed("FIXME");
        }
        catch (storage::MissingKey const&)
        {
          if (!block.validate())
            throw ValidationFailed("FIXME");
        }
        elle::Buffer data;
        {
          elle::IOStream s(data.ostreambuf());
          Serializer::SerializerOut output(s, false);
          output.serialize_forward(block);
        }
        on_store(block, mode);
        this->_storage->set(block.address(), data,
                            mode == STORE_ANY || mode == STORE_INSERT,
                            mode == STORE_ANY || mode == STORE_UPDATE);
      }

      std::unique_ptr<blocks::Block>
      Local::fetch(Address address) const
      {
        auto data = this->_storage->get(address);
        elle::IOStream s(data.istreambuf());
        Serializer::SerializerIn input(s, false);
        input.set_context<Doughnut*>(this->_doughnut.get());
        auto res = input.deserialize<std::unique_ptr<blocks::Block>>();
        on_fetch(address, res);
        return std::move(res);
      }

      void
      Local::remove(Address address)
      {
        ELLE_DEBUG("remove %x", address);
        this->_storage->erase(address);
        on_remove(address);
      }

      /*-------.
      | Server |
      `-------*/

      void
      Local::serve()
      {
        this->_server_barrier.open();
      }

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
                 std::function<void (blocks::Block const& data, StoreMode)>(
                   [this] (blocks::Block const& block, StoreMode mode)
                   {
                     return this->store(block, mode);
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
        reactor::wait(this->_server_barrier);
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
                rpcs.set_context<Doughnut*>(this->_doughnut.get());
                rpcs.serve(**socket);
              });
          }
        };
      }
    }
  }
}
