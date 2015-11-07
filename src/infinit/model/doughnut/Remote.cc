#include <infinit/model/doughnut/Remote.hh>

#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/utils.hh>

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

      Remote::Remote(Doughnut& doughnut, Address id,
                     std::string const& host, int port)
        : Super(std::move(id))
        , _doughnut(doughnut)
        , _socket()
        , _serializer()
        , _channels()
        , _connection_thread()
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

      Remote::Remote(Doughnut& doughnut, Address id,
                     boost::asio::ip::tcp::endpoint endpoint)
        : Super(std::move(id))
        , _doughnut(doughnut)
        , _socket(nullptr)
        , _serializer()
        , _channels()
        , _connection_thread()
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

      Remote::Remote(Doughnut& doughnut, Address id,
                     boost::asio::ip::udp::endpoint endpoint,
                     reactor::network::UTPServer& server)
        : Super(std::move(id))
        , _doughnut(doughnut)
        , _utp_socket(nullptr)
        , _serializer()
        , _channels()
        , _connection_thread()
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

      Remote::Remote(Doughnut& doughnut, Address id,
                     std::vector<boost::asio::ip::udp::endpoint> endpoints,
                     std::string const& peer_id,
                     reactor::network::UTPServer& server)
        : Super(std::move(id))
        , _doughnut(doughnut)
        , _utp_socket(nullptr)
        , _serializer()
        , _channels()
        , _connection_thread()
      {
        this->_connect(
          elle::sprintf("%s", endpoints),
          [this, endpoints, peer_id, &server] () -> std::iostream&
          {
            this->_utp_socket.reset(new reactor::network::UTPSocket(server));
            this->_utp_socket->connect(peer_id, endpoints);
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
        this->_connector = socket;
        this->_endpoint = endpoint;
        this->_connection_thread.reset(
          new reactor::Thread(
            elle::sprintf("%s connection", *this),
            [this, endpoint, socket]
            {
              try
              {
                ELLE_TRACE("Connecting socket");
                this->_serializer.reset(
                  new protocol::Serializer(socket(), false));
                ELLE_TRACE("Establishing channel");
                this->_channels.reset(
                  new protocol::ChanneledStream(*this->_serializer));
                static bool disable_key = getenv("INFINIT_RPC_DISABLE_CRYPTO");
                if (disable_key)
                {
                  ELLE_TRACE("Exchanging keys");
                  _key_exchange();
                }
                ELLE_TRACE("Connected");
              }
              catch (reactor::network::Exception const&)
              { // Upper layers may retry on network::Exception
                throw;
              }
              catch (elle::Error const&)
              {
                elle::throw_with_nested(
                  elle::Error(
                    elle::sprintf("connection failed to %s", endpoint)));
              }
            },
            reactor::Thread::managed = true));
      }

      void
      Remote::connect(elle::DurationOpt timeout)
      {
        if (!reactor::wait(*this->_connection_thread, timeout))
          throw reactor::network::TimeOut();
      }

      void
      Remote::reconnect(elle::DurationOpt timeout)
      {
        this->_credentials = {};
        _connect(this->_endpoint, this->_connector);
        connect(timeout);
      }

      /*-------.
      | Blocks |
      `-------*/

      void Peer::connect_retry()
      {
        static const int timeout_sec =
        std::stoi(elle::os::getenv("INFINIT_CONNECT_TIMEOUT", "0"));
        elle::DurationOpt timeout;
        if (timeout_sec)
          timeout = boost::posix_time::seconds(timeout_sec);
        for (int i=0; i<5; ++i)
        {
          try
          {
            if (i == 0)
              this->connect(timeout);
            else
              this->reconnect(timeout);
            return;
          }
          catch(reactor::network::Exception const& e)
          {
            ELLE_TRACE("attempt %s: remote network exception %s", i, e);
            if (i == 4)
              throw;
            reactor::sleep(boost::posix_time::milliseconds(20*pow(2,i)));
          }
        }
      }

      void
      Remote::_key_exchange()
      {
        // challenge, token
        typedef std::pair<elle::Buffer, elle::Buffer> Challenge;
        ELLE_TRACE("starting key exchange");
        RPC<std::pair<Challenge, std::unique_ptr<Passport>>(Passport const&)>
          auth_syn("auth_syn", *this->_channels, nullptr);
        auto challenge_passport = auth_syn(this->_doughnut.passport());
        auto& remote_passport = challenge_passport.second;
        ELLE_ASSERT(remote_passport);
        // validate res
        bool check = remote_passport->verify(this->_doughnut.owner());
        ELLE_TRACE("got remote passport, check=%s", check);
        if (!check)
        {
          ELLE_LOG("Passport validation failed.");
          throw elle::Error("Passport validation failed");
        }
        // sign the challenge
        auto signed_challenge = this->_doughnut.keys().k().sign(
          challenge_passport.first.first,
          infinit::cryptography::rsa::Padding::pss,
          infinit::cryptography::Oneway::sha256);
        // generate, seal
        // dont set _key yet so that our 2 rpcs are in cleartext
        auto key = infinit::cryptography::secretkey::generate(256);
        ELLE_TRACE("passwording...");
        elle::Buffer password = key.password();
        ELLE_TRACE("sealing...");
        auto sealed_key = remote_passport->user().seal(password,
          infinit::cryptography::Cipher::aes256,
          infinit::cryptography::Mode::cbc);
        ELLE_TRACE("Invoking auth_ack...");
        RPC<bool(elle::Buffer const&, elle::Buffer const&, elle::Buffer const&)>
        auth_ack("auth_ack", *this->_channels, nullptr);
        auth_ack(sealed_key, challenge_passport.first.second, signed_challenge);
        _credentials = std::move(password);
        ELLE_TRACE("...done");
      }

      void
      Remote::store(blocks::Block const& block, StoreMode mode)
      {
        ELLE_ASSERT(&block);
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        this->connect_retry();
        RPC<void (blocks::Block const&, StoreMode)> store
        ("store", *this->_channels, &this->_credentials);
        store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Remote::fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
        elle::unconst(this)->connect_retry();
        RPC<std::unique_ptr<blocks::Block> (Address)> fetch(
          "fetch", *const_cast<Remote*>(this)->_channels,
          &const_cast<Remote*>(this)->_credentials);
        fetch.set_context<Doughnut*>(&this->_doughnut);
        return fetch(address);
      }

      void
      Remote::remove(Address address)
      {
        ELLE_TRACE_SCOPE("%s: remove %x", *this, address);
        this->connect_retry();
        RPC<void (Address)> remove("remove", *this->_channels,
          &this->_credentials);
        remove(address);
      }

      /*----------.
      | Printable |
      `----------*/

      void
      Remote::print(std::ostream& stream) const
      {
        auto name = elle::type_info(*this).name();
        if (this->_socket)
          elle::fprintf(stream, "%s(%s)", name, *this->_socket);
        else if (this->_utp_socket)
          elle::fprintf(stream, "%s(%s)", name, *this->_utp_socket);
        else
          elle::fprintf(stream, "%s(%s)", name, this);
      }
    }
  }
}
