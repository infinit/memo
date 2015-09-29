#include <infinit/model/doughnut/Remote.hh>

#include <elle/log.hh>

#include <reactor/thread.hh>
#include <reactor/scheduler.hh>

#include <infinit/RPC.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote")

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      Remote::Remote(Doughnut& doughnut, std::string const& host, int port)
        : _doughnut(doughnut)
        , _socket()
        , _serializer()
        , _channels()
        , _connection_thread()
        , _connected()
        , _connection_exception()
      {
        this->_connect(
          elle::sprintf("%s:%s", host, port),
          [host, port, this] () -> std::iostream&
          {
            this->_socket.reset(
              new reactor::network::TCPSocket(host, port));
            return *this->_socket;
          });
      }

      Remote::Remote(Doughnut& doughnut,
                     boost::asio::ip::tcp::endpoint endpoint)
        : _doughnut(doughnut)
        , _socket(nullptr)
        , _serializer()
        , _channels()
        , _connection_thread()
        , _connected()
        , _connection_exception()
      {
        this->_connect(
          elle::sprintf("%s", endpoint),
          [endpoint, this] () -> std::iostream&
          {
            this->_socket.reset(
              new reactor::network::TCPSocket(endpoint));
            return *this->_socket;
          });
      }

      Remote::Remote(Doughnut& doughnut,
                     boost::asio::ip::udp::endpoint endpoint,
                     reactor::network::UTPServer& server)
        : _doughnut(doughnut)
        , _utp_socket(nullptr)
        , _serializer()
        , _channels()
        , _connection_thread()
        , _connected()
        , _connection_exception()
      {
        this->_connect(
          elle::sprintf("%s", endpoint),
          [this, endpoint, &server] () -> std::iostream&
          {
            this->_utp_socket.reset(
              new reactor::network::UTPSocket(
                server, endpoint.address().to_string(), endpoint.port()));
            return *this->_utp_socket;
          });
      }

      Remote::~Remote()
      {}

      /*-----------.
      | Networking |
      `-----------*/

      void
      Remote::_connect(
        std::string endpoint,
        std::function <std::iostream& ()> const& socket)
      {
        this->_connection_thread.reset(
          new reactor::Thread(
            elle::sprintf("%s connection", *this),
            [this, endpoint, socket]
            {
              try
              {
                this->_serializer.reset(new protocol::Serializer(socket()));
                this->_channels.reset(
                  new protocol::ChanneledStream(*this->_serializer));
                this->_connected.open();
              }
              catch (elle::Error const&)
              {
                try
                {
                  elle::throw_with_nested(
                    elle::Error(
                      elle::sprintf("connection failed to %s", endpoint)));
                }
                catch (...)
                {
                  this->_connection_exception = std::current_exception();
                }
              }
            }));
      }

      void
      Remote::connect()
      {
        if (this->_connection_exception)
          std::rethrow_exception(this->_connection_exception);
        reactor::wait(this->_connected);
      }

      /*-------.
      | Blocks |
      `-------*/

      void
      Remote::store(blocks::Block const& block, StoreMode mode)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        this->connect();
        RPC<void (blocks::Block const&, StoreMode)> store
          ("store", *this->_channels);
        store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Remote::fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
        const_cast<Remote*>(this)->connect();
        RPC<std::unique_ptr<blocks::Block> (Address)> fetch(
          "fetch", *const_cast<Remote*>(this)->_channels);
        fetch.set_context<Doughnut*>(&this->_doughnut);
        return fetch(address);
      }

      void
      Remote::remove(Address address)
      {
        ELLE_TRACE_SCOPE("%s: remove %x", *this, address);
        this->connect();
        RPC<void (Address)> remove("remove", *this->_channels);
        remove(address);
      }

      /*----------.
      | Printable |
      `----------*/

      void
      Remote::print(std::ostream& stream) const
      {
        if (this->_socket)
          elle::fprintf(stream, "Remote(%s)", *this->_socket);
        else if (this->_utp_socket)
          elle::fprintf(stream, "Remote(%s)", *this->_utp_socket);
        else
          elle::fprintf(stream, "Remote()");
      }
    }
  }
}
