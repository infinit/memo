#include <infinit/model/doughnut/Remote.hh>

#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/utils.hh>
#include <elle/bench.hh>

#include <reactor/Scope.hh>
#include <reactor/thread.hh>
#include <reactor/scheduler.hh>

#include <infinit/RPC.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote")

#define BENCH(name)                                      \
  static elle::Bench bench("bench.remote." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-----.
      | Auth |
      `-----*/

      Remote::Auth::Auth(Address id,
                         Challenge challenge,
                         Passport passport)
        : id(std::move(id))
        , challenge(std::move(challenge))
        , passport(std::move(passport))
      {}

      Remote::Auth::Auth(elle::serialization::SerializerIn& input)
        : id(input.deserialize<Address>("id"))
        , challenge(input.deserialize<Challenge>("challenge"))
        , passport(input.deserialize<Passport>("passport"))
      {}


      /*-------------.
      | Construction |
      `-------------*/

      Remote::Remote(Doughnut& dht,
                     Address id,
                     Endpoints endpoints,
                     boost::optional<reactor::network::UTPServer&> server,
                     boost::optional<EndpointsRefetcher> const& refetch,
                     Protocol protocol)
        : Super(dht, std::move(id))
        , _socket(nullptr)
        , _serializer()
        , _channels()
        , _connected(false)
        , _reconnecting(false)
        , _reconnection_id(0)
        , _endpoints(std::move(endpoints))
        , _utp_server(server)
        , _protocol(protocol)
        , _connection_thread()
        , _fast_fail(false)
      {
        ELLE_ASSERT(server || protocol != Protocol::utp);
        this->_connect();
      }

      Remote::~Remote()
      {}

      /*-----------.
      | Networking |
      `-----------*/

      void
      Remote::_connect()
      {
        static bool disable_key = getenv("INFINIT_RPC_DISABLE_CRYPTO");
        ELLE_TRACE_SCOPE("%s: connect", *this);
        ++this->_reconnection_id;
        if (this->_connection_thread)
          this->_connection_thread->terminate_now();
        this->_connection_thread.reset(
          new reactor::Thread(
            elle::sprintf("%s connection", this),
            [this]
            {
              this->_connected = false;
              auto handshake = [&] (std::unique_ptr<std::iostream> socket)
                {
                  auto serializer = elle::make_unique<protocol::Serializer>(
                    *socket,
                    elle_serialization_version(this->_doughnut.version()),
                    false);
                  auto channels =
                    elle::make_unique<protocol::ChanneledStream>(*serializer);
                  if (!disable_key)
                    this->_key_exchange(*channels);
                  ELLE_TRACE("connected");
                  this->_socket = std::move(socket);
                  this->_serializer = std::move(serializer);
                  this->_channels = std::move(channels);
                  this->_connected = true;
                };
              auto umbrella = [&] (std::function<void ()> const& f)
                {
                  return [f]
                  {
                    try
                    {
                      f();
                    }
                    catch (reactor::network::Exception const&)
                    {
                      // ignored
                    }
                  };
                };
              elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
              {
                if (this->_protocol == Protocol::tcp ||
                    this->_protocol == Protocol::all)
                  for (auto const& e: this->_endpoints)
                    scope.run_background(
                      elle::sprintf("%s: connect to tcp://%s", this, e),
                      umbrella(
                        [&]
                        {
                          using reactor::network::TCPSocket;
                          handshake(elle::make_unique<TCPSocket>(e.tcp()));
                          scope.terminate_now();
                        }));
                if (this->_protocol == Protocol::utp ||
                    this->_protocol == Protocol::all)
                  scope.run_background(
                    elle::sprintf("%s: connect to utp://%s",
                                  this, this->_endpoints),
                    umbrella(
                      [&]
                      {
                        std::string cid;
                        if (this->id() != Address::null)
                          cid = elle::sprintf("%x", this->id());
                        auto socket =
                          elle::make_unique<reactor::network::UTPSocket>(
                            *this->_utp_server);
                        socket->connect(cid, this->_endpoints.udp());
                        handshake(std::move(socket));
                        scope.terminate_now();
                      }));
                reactor::wait(scope);
              };
              if (!this->_connected)
                elle::err("connection to %f failed", this->_endpoints);
            },
            reactor::Thread::managed = true));
      }

      void
      Remote::connect(elle::DurationOpt timeout)
      {
        do
        {
          auto start = boost::posix_time::microsec_clock::universal_time();
          if (!reactor::wait(*this->_connection_thread, timeout))
            throw reactor::network::TimeOut();
          // Either connect succeeded, or it was restarted
          if (timeout)
          {
            timeout = *timeout -
              (boost::posix_time::microsec_clock::universal_time() - start);
            if (timeout->is_negative() && !_connected)
              throw reactor::network::TimeOut();
          }
        }
        while (!this->_connected);
      }

      void
      Remote::reconnect(elle::DurationOpt timeout)
      {
        if (!this->_reconnecting)
        {
          auto lock = elle::scoped_assignment(this->_reconnecting, true);
          ELLE_TRACE("%s: reconnect", this);
          this->_credentials = {};
          if (this->_refetch_endpoints)
            if (auto eps = this->_refetch_endpoints())
              this->_endpoints = std::move(eps.get());
          this->_connect();
        }
        else
          ELLE_DEBUG("skip overlapped reconnect");
        connect(timeout);
      }

      /*-------.
      | Blocks |
      `-------*/

      static
      std::pair<Remote::Challenge, std::unique_ptr<Passport>>
      _auth_0_3(Remote& self, protocol::ChanneledStream& channels)
      {
        using AuthSyn = std::pair<Remote::Challenge, std::unique_ptr<Passport>>
          (Passport const&);
        RPC<AuthSyn> auth_syn(
          "auth_syn", channels, self.doughnut().version());
        return auth_syn(self.doughnut().passport());
      }

      static
      std::pair<Remote::Challenge, std::unique_ptr<Passport>>
      _auth_0_4(Remote& self, protocol::ChanneledStream& channels)
      {
        using AuthSyn = std::pair<Remote::Challenge, std::unique_ptr<Passport>>
          (Passport const&, elle::Version const&);
        RPC<AuthSyn> auth_syn(
          "auth_syn", channels, self.doughnut().version());
        auth_syn.set_context<Doughnut*>(&self.doughnut());
        auto version = self.doughnut().version();
        // 0.5.0 and 0.6.0 compares the full version it receives for
        // compatibility instead of dropping the subminor component. Set
        // it to 0.
        return auth_syn(
          self.doughnut().passport(),
          elle::Version(version.major(), version.minor(), 0));
      }

      static
      Remote::Auth
      _auth_0_7(Remote& self, protocol::ChanneledStream& channels)
      {
        using AuthSyn = Remote::Auth (Passport const&, elle::Version const&);
        RPC<AuthSyn> auth_syn(
          "auth_syn", channels, self.doughnut().version());
        auth_syn.set_context<Doughnut*>(&self.doughnut());
        return auth_syn(self.doughnut().passport(), self.doughnut().version());
      }

      void
      Remote::_key_exchange(protocol::ChanneledStream& channels)
      {
        ELLE_TRACE_SCOPE("%s: exchange keys", *this);
        try
        {
          auto challenge_passport = [&]
          {
            if (this->_doughnut.version() >= elle::Version(0, 7, 0))
            {
              auto res = _auth_0_7(*this, channels);
              return std::make_pair(
                res.challenge,
                elle::make_unique<Passport>(std::move(res.passport)));
            }
            else if (this->_doughnut.version() >= elle::Version(0, 4, 0))
              return _auth_0_4(*this, channels);
            else
              return _auth_0_3(*this, channels);
          }();
          auto& remote_passport = challenge_passport.second;
          ELLE_ASSERT(remote_passport);
          if (!this->_doughnut.verify(*remote_passport, false, false, false))
          {
            auto msg = elle::sprintf(
              "passport validation failed for %s", this->id());
            ELLE_WARN("%s", msg);
            throw elle::Error(msg);
          }
          if (!remote_passport->allow_storage())
          {
            auto msg = elle::sprintf(
              "%s: Peer passport disallows storage", *this);
            ELLE_WARN("%s", msg);
            throw elle::Error(msg);
          }
          ELLE_DEBUG("got valid remote passport");
          // sign the challenge
          auto signed_challenge = this->_doughnut.keys().k().sign(
            challenge_passport.first.first,
            infinit::cryptography::rsa::Padding::pss,
            infinit::cryptography::Oneway::sha256);
          // generate, seal
          // dont set _key yet so that our 2 rpcs are in cleartext
          auto key = infinit::cryptography::secretkey::generate(256);
          elle::Buffer password = key.password();
          auto sealed_key =
            remote_passport->user().seal(password,
                                         infinit::cryptography::Cipher::aes256,
                                         infinit::cryptography::Mode::cbc);
          ELLE_DEBUG("acknowledge authentication")
          {
            RPC<bool (elle::Buffer const&,
                      elle::Buffer const&,
                      elle::Buffer const&)>
              auth_ack("auth_ack",
                       channels, this->_doughnut.version(), nullptr);
            auth_ack(sealed_key,
                     challenge_passport.first.second,
                     signed_challenge);
            this->_credentials = std::move(password);
          }
        }
        catch (elle::Error& e)
        {
          ELLE_WARN("key exchange failed with %s: %s",
                    this->id(), elle::exception_string());
          throw;
        }
      }

      void
      Remote::store(blocks::Block const& block, StoreMode mode)
      {
        BENCH("store");
        ELLE_ASSERT(&block);
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        auto store = make_rpc<void (blocks::Block const&, StoreMode)>("store");
        store.set_context<Doughnut*>(&this->_doughnut);
        store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Remote::_fetch(Address address,
                    boost::optional<int> local_version) const
      {
        BENCH("fetch");
        auto fetch = elle::unconst(this)->make_rpc<
          std::unique_ptr<blocks::Block>(Address,
                                         boost::optional<int>)>("fetch");
        fetch.set_context<Doughnut*>(&this->_doughnut);
        return fetch(std::move(address), std::move(local_version));
      }

      void
      Remote::remove(Address address, blocks::RemoveSignature rs)
      {
        BENCH("remove");
        ELLE_TRACE_SCOPE("%s: remove %x", *this, address);
        if (this->_doughnut.version() >= elle::Version(0, 4, 0))
        {
          auto remove = make_rpc<void (Address, blocks::RemoveSignature)>
            ("remove");
          remove.set_context<Doughnut*>(&this->_doughnut);
          remove(address, rs);
        }
        else
        {
          auto remove = make_rpc<void (Address)>
            ("remove");
          remove(address);
        }
      }
    }
  }
}
