#include <infinit/model/doughnut/Local.hh>

#include <elle/log.hh>
#include <elle/utility/Move.hh>

#include <reactor/Scope.hh>

#include <infinit/RPC.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/storage/MissingKey.hh>

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

      template <typename T, typename Serializer>
      T
      deserialize(elle::Buffer const& data)
      {
        elle::IOStream s(data.istreambuf());
        typename Serializer::SerializerIn input(s);
        return input.deserialize<T>();
      }

      void
      Local::store(blocks::Block const& block, StoreMode mode)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        try
        {
          auto previous =
            deserialize<std::unique_ptr<blocks::Block>,
                        elle::serialization::Json>
            (this->_storage->get(block.address()));
          if (!block.validate(*previous))
            throw elle::Error("block validation failed");
        }
        catch (storage::MissingKey const&)
        {
          if (!block.validate())
            throw elle::Error("block validation failed");
        }
        elle::Buffer data;
        {
          elle::IOStream s(data.ostreambuf());
          elle::serialization::json::SerializerOut output(s);
          output.serialize_forward(block);
        }
        this->_storage->set(block.address(), data,
                            mode == STORE_ANY || mode == STORE_INSERT,
                            mode == STORE_ANY || mode == STORE_UPDATE);
      }

      std::unique_ptr<blocks::Block>
      Local::fetch(Address address) const
      {
        auto data = this->_storage->get(address);
        elle::IOStream s(data.istreambuf());
        elle::serialization::json::SerializerIn input(s);
        return input.deserialize<std::unique_ptr<blocks::Block>>();
      }

      void
      Local::remove(Address address)
      {
        ELLE_DEBUG("remove %x", address);
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
