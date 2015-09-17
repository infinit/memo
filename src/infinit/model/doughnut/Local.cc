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

      Local::Local(std::unique_ptr<storage::Storage> storage,
                   int port,
                   Protocol p)
        : _storage(std::move(storage))
        , _doughnut(nullptr)
      {
        if (p == Protocol::tcp || p == Protocol::all)
        {
          this->_server_thread = elle::make_unique<reactor::Thread>(
            elle::sprintf("%s server", *this),
            [this] { this->_serve(); });
          this->_server = elle::make_unique<reactor::network::TCPServer>();
          this->_server->listen(port);
        }
        if (p == Protocol::utp || p == Protocol::all)
        {
          this->_utp_server_thread = elle::make_unique<reactor::Thread>(
            elle::sprintf("%s utp server", *this),
            [this] { this->_serve_utp(); });
          this->_utp_server = elle::make_unique<reactor::network::UTPServer>();
          // FIXME: kelips already use the Local's port on udp for its
          // gossip protocol, so use a fixed delta until we advertise
          // tcp and utp endpoints separately
          if (this->_server)
            port = this->_server->port() + 100;
          else if (port)
            port += 100;
          this->_utp_server->listen(port);
        }
        ELLE_TRACE("%s: listen on %s", *this, this->server_endpoint());
      }

      Local::~Local()
      {
        ELLE_TRACE_SCOPE("%s: terminate", *this);
        if (this->_server_thread)
          this->_server_thread->terminate_now();
        if (this->_utp_server_thread)
          this->_utp_server_thread->terminate_now();
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
          input.set_context<Doughnut*>(this->_doughnut);
          auto previous = input.deserialize<std::unique_ptr<blocks::Block>>();
          ELLE_DEBUG("%s: validate block against previous version", *this)
            if (auto res = block.validate(*previous)); else
              throw ValidationFailed(res.reason());
        }
        catch (storage::MissingKey const&)
        {
          ELLE_DEBUG("%s: validate block", *this)
            if (auto res = block.validate()); else
              throw ValidationFailed(res.reason());
        }
        elle::Buffer data;
        {
          elle::IOStream s(data.ostreambuf());
          Serializer::SerializerOut output(s, false);
          output.serialize_forward(block);
        }
        this->_storage->set(block.address(), data,
                            mode == STORE_ANY || mode == STORE_INSERT,
                            mode == STORE_ANY || mode == STORE_UPDATE);
        on_store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Local::fetch(Address address) const
      {
        auto data = this->_storage->get(address);
        elle::IOStream s(data.istreambuf());
        Serializer::SerializerIn input(s, false);
        input.set_context<Doughnut*>(this->_doughnut);
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
        if (this->_server)
          return this->_server->local_endpoint();
        else if (this->_utp_server)
        {
          auto ep = this->_utp_server->local_endpoint();
          return reactor::network::TCPServer::EndPoint(ep.address(), ep.port()-100);
        }
        else throw elle::Error("Local not listening on any endpoint");
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
            auto socket = elle::utility::move_on_copy(this->_server->accept());
            auto name = elle::sprintf("%s: %s server", *this, **socket);
            scope.run_background(
              name,
              [this, socket, &rpcs]
              {
                rpcs.set_context<Doughnut*>(this->_doughnut);
                rpcs.serve(**socket);
              });
          }
        };
      }



      void
      Local::_serve_utp()
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
            auto socket = elle::utility::move_on_copy(this->_utp_server->accept());
            auto name = elle::sprintf("%s: %s server", *this, **socket);
            scope.run_background(
              name,
              [this, socket, &rpcs]
              {
                rpcs.set_context<Doughnut*>(this->_doughnut);
                protocol::Serializer serializer(**socket);
                rpcs.serve(serializer);
              });
          }
        };
      }

    }
  }
}

namespace elle
{
  namespace serialization
  {
    using namespace infinit::model::doughnut;
    std::string
    Serialize<Local::Protocol>::convert(
      Local::Protocol p)
    {
      switch (p)
      {
        case Local::Protocol::tcp:
          return "tcp";
        case Local::Protocol::utp:
          return "utp";
        case Local::Protocol::all:
          return "all";
        default:
          elle::unreachable();
      }
    }

    Local::Protocol
    Serialize<Local::Protocol>::convert(std::string const& repr)
    {
      if (repr == "tcp")
        return Local::Protocol::tcp;
      else if (repr == "utp")
        return Local::Protocol::utp;
      else if (repr == "all")
        return Local::Protocol::all;
      else
        throw Error("Expected one of tcp, utp, all,  got '" + repr + "'");
    }
  }
}
