#include <infinit/model/doughnut/Local.hh>

#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/network/Interface.hh>
#include <elle/utility/Move.hh>

#include <cryptography/random.hh>
#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/rsa/Padding.hh>

#include <reactor/Scope.hh>
#include <reactor/network/utp-server.hh>

#include <infinit/model/Endpoints.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/HandshakeFailed.hh>
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

      class BindException
        : public std::runtime_error
      {
      public:
        BindException(const std::string& s)
        : std::runtime_error(s)
        {}
      };

      Local::Local(Doughnut& dht,
                   Address id,
                   std::unique_ptr<storage::Storage> storage,
                   int port,
                   boost::optional<boost::asio::ip::address> listen_address,
                   Protocol p)
        : Super(dht, std::move(id))
        , _storage(std::move(storage))
      {
        std::unique_ptr<reactor::network::TCPServer> old_server;
        int num_run = 0;
        while (true)
        {
          if (++num_run > 3)
          { // we are looping on the same ports, try one at random instead
            port = 1025 + (rand()%(65536 - 1025));
          }
          try
          {
            ELLE_TRACE_SCOPE("%s: construct", this);
            bool v6 = elle::os::getenv("INFINIT_NO_IPV6", "").empty()
                && dht.version() >= elle::Version(0, 7, 0);
            if (p == Protocol::tcp || p == Protocol::all)
            {
              this->_server = elle::make_unique<reactor::network::TCPServer>();
              if (listen_address)
                this->_server->listen(*listen_address, port, v6);
              else
                this->_server->listen(port, v6);
              this->_server_thread = elle::make_unique<reactor::Thread>(
                elle::sprintf("%s", this),
                [this] { this->_serve_tcp(); });
              ELLE_LOG("%s: listen on tcp://%s",
                       this, this->_server->local_endpoint());
            }
            if (p == Protocol::utp || p == Protocol::all)
            {
              int udp_port = port;
              this->_utp_server =
                elle::make_unique<reactor::network::UTPServer>();
              if (this->_server)
                udp_port = this->_server->port();
              try
              {
                if (listen_address)
                  this->_utp_server->listen(*listen_address, udp_port, v6);
                else
                  this->_utp_server->listen(udp_port, v6);
              }
              catch (std::exception const& e)
              {
                if (!port)
                  throw BindException(e.what());
                else
                  throw; // port was specified in args, no retry
              }
              this->_utp_server_thread = elle::make_unique<reactor::Thread>(
                elle::sprintf("%s UTP", *this),
                [this] { this->_serve_utp(); });
              ELLE_LOG("%s: listen on utp://%s",
                       this, this->_utp_server->local_endpoint());
            }
            break;
          }
          catch (BindException const& e)
          {
            ELLE_WARN("%s: bind failed with: %s, retrying", this, e.what());
            if (this->_server_thread)
              this->_server_thread->terminate_now();
            // Keep the TCPServer alive so that our next attempts will pick
            // a different port.
            old_server = std::move(this->_server);
          }
          catch (std::exception const& e)
          {
            ELLE_WARN("%s: initialization failed with: %s", this, e.what());
            if (this->_server_thread)
              this->_server_thread->terminate_now();
            if (this->_utp_server_thread)
              this->_utp_server_thread->terminate_now();
            throw;
          }
        }
      }

      Local::~Local()
      {
        ELLE_TRACE_SCOPE("%s: destruct", this);
        this->_cleanup();
      }

      void
      Local::initialize()
      {}

      void
      Local::_cleanup()
      {
        if (this->_server_thread)
        {
          this->_server_thread->terminate_now();
          this->_server_thread.reset();
        }
        if (this->_utp_server_thread)
        {
          this->_utp_server_thread->terminate_now();
          this->_utp_server_thread.reset();
        }
      }

      elle::Version const&
      Local::version() const
      {
        return this->_doughnut.version();
      }

      /*-------.
      | Blocks |
      `-------*/

      void
      Local::store(blocks::Block const& block, StoreMode mode)
      {
        ELLE_ASSERT(&block);
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        ELLE_DEBUG("%s: validate block", *this)
          if (auto res = block.validate(this->doughnut(), true)); else
            throw ValidationFailed(res.reason());
        try
        {
          auto previous_buffer = this->_storage->get(block.address());
          elle::IOStream s(previous_buffer.istreambuf());
          typename elle::serialization::binary::SerializerIn input(s);
          input.set_context<Doughnut*>(&this->_doughnut);
          auto previous = input.deserialize<std::unique_ptr<blocks::Block>>();
          if (auto* mblock = dynamic_cast<blocks::MutableBlock const*>(&block))
          {
            auto mprevious =
              dynamic_cast<blocks::MutableBlock const*>(previous.get());
            if (!mprevious)
              throw ValidationFailed("overwriting a non-mutable block");
            if (mblock->version() <= mprevious->version())
              throw Conflict(
                elle::sprintf("version %s is not superior to current version %s",
                              mblock->version(), mprevious->version()),
                std::move(previous));
          }
          auto vr = previous->validate(this->doughnut(), block);
          if (!vr)
            if (vr.conflict())
              throw Conflict(vr.reason(), std::move(previous));
            else
              throw ValidationFailed(vr.reason());
        }
        catch (storage::MissingKey const&)
        {}
        elle::Buffer data = [&block]
          {
            elle::Buffer res;
            elle::IOStream s(res.ostreambuf());
            Serializer::SerializerOut output(s);
            auto ptr = &block;
            output.serialize_forward(ptr);
            return res;
          }();
        try
        {
          this->_storage->set(block.address(), data,
                              mode == STORE_INSERT,
                              mode == STORE_UPDATE);
        }
        catch (storage::MissingKey const&)
        {
          throw MissingBlock(block.address());
        }
        this->_on_store(block);
      }

      std::unique_ptr<blocks::Block>
      Local::_fetch(Address address, boost::optional<int> local_version) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %f", this, address);
        elle::Buffer data;
        try
        {
          data = this->_storage->get(address);
        }
        catch (storage::MissingKey const& e)
        {
          throw MissingBlock(e.key());
        }
        ELLE_DUMP("data: %s", data.string());
        elle::serialization::Context ctx;
        ctx.set<Doughnut*>(&this->_doughnut);
        auto res = elle::serialization::binary::deserialize<
          std::unique_ptr<blocks::Block>>(data, true, ctx);
        this->_on_fetch(address, res);
        return res;
      }

      void
      Local::remove(Address address, blocks::RemoveSignature rs)
      {
        ELLE_DEBUG("remove %x", address);
        try
        {
          if (this->_doughnut.version() >= elle::Version(0, 4, 0))
          {
            auto previous_buffer = this->_storage->get(address);
            elle::IOStream s(previous_buffer.istreambuf());
            typename elle::serialization::binary::SerializerIn input(s);
            input.set_context<Doughnut*>(&this->_doughnut);
            auto previous = input.deserialize<std::unique_ptr<blocks::Block>>();
            auto val = previous->validate_remove(this->doughnut(), rs);
            if (!val)
              if (val.conflict())
                throw Conflict(val.reason(), previous->clone());
              else
                throw ValidationFailed(val.reason());
          }
          this->_storage->erase(address);
        }
        catch (storage::MissingKey const& k)
        {
          throw MissingBlock(k.key());
        }
        this->_on_remove(address);
      }

      /*-----.
      | Keys |
      `-----*/

      std::vector<cryptography::rsa::PublicKey>
      Local::_resolve_keys(std::vector<int> ids)
      {
        std::vector<infinit::cryptography::rsa::PublicKey> res;
        for (auto const& h: ids)
          res.emplace_back(*this->doughnut().resolve_key(h));
        return res;
      }

      std::unordered_map<int, cryptography::rsa::PublicKey>
      Local::_resolve_all_keys()
      {
        std::unordered_map<int, cryptography::rsa::PublicKey> res;
        for (auto const& k: this->doughnut().key_cache())
          res.emplace(k.hash, *k.key);
        return res;
      }

      /*-------.
      | Server |
      `-------*/

      Endpoint
      Local::server_endpoint()
      {
        if (this->_server)
          return this->_server->local_endpoint();
        else if (this->_utp_server)
          return this->_utp_server->local_endpoint();
        else
          elle::err("local not listening on any endpoint");
      }

      Endpoints
      Local::server_endpoints()
      {
        bool v6 = elle::os::getenv("INFINIT_NO_IPV6", "").empty()
                  && this->doughnut().version() >= elle::Version(0, 7, 0);
        auto ep = this->server_endpoint();
        if (ep.address() != boost::asio::ip::address_v6::any()
         && ep.address() != boost::asio::ip::address_v4::any())
          return { ep };
        Endpoints res;
        auto filter = (elle::network::Interface::Filter::only_up |
                       elle::network::Interface::Filter::no_loopback |
                       elle::network::Interface::Filter::no_autoip);
        for (auto const& itf: elle::network::Interface::get_map(filter))
        {
          if (!itf.second.ipv4_address.empty()
              && itf.second.ipv4_address != boost::asio::ip::address_v4::any().to_string())
          {
            res.push_back(reactor::network::TCPServer::EndPoint(
              boost::asio::ip::address::from_string(itf.second.ipv4_address),
              ep.port()));
          }
          if (v6)
          for (auto const& ip6: itf.second.ipv6_address)
          {
            if (ip6 != boost::asio::ip::address_v6::any().to_string())
              res.push_back(reactor::network::TCPServer::EndPoint(
                boost::asio::ip::address::from_string(ip6),
                ep.port()));
          }
        }
        return res;
      }

      void
      Local::_require_auth(RPCServer& rpcs, bool write_op)
      {
        static bool disable = getenv("INFINIT_RPC_DISABLE_CRYPTO");
        if (disable)
          return;
        if (!rpcs._key)
          throw elle::Error("Authentication required");
        if (write_op && !this->_passports.at(&rpcs).allow_write())
          throw elle::Error("Write permission denied.");
      }

      void
      Local::_register_rpcs(RPCServer& rpcs)
      {
        rpcs.set_context(this);
        rpcs._destroying.connect([this] ( RPCServer* rpcs)
          {
            this->_passports.erase(rpcs);
          });
        rpcs.add("store",
                 std::function<void (blocks::Block const& data, StoreMode)>(
                   [this,&rpcs] (blocks::Block const& block, StoreMode mode)
                   {
                     this->_require_auth(rpcs, true);
                     return this->store(block, mode);
                   }));
        rpcs.add("fetch",
                 std::function<
                   std::unique_ptr<blocks::Block> (Address,
                                                   boost::optional<int>)>(
                  [this, &rpcs] (Address address, boost::optional<int> local_version)
                  {
                    this->_require_auth(rpcs, false);
                    return this->fetch(address, local_version);
                  }));
        if (this->_doughnut.version() >= elle::Version(0, 4, 0))
          rpcs.add("remove",
                  std::function<void (Address address, blocks::RemoveSignature)>(
                    [this, &rpcs] (Address address, blocks::RemoveSignature rs)
                    {
                      this->_require_auth(rpcs, true);
                      this->remove(address, rs);
                    }));
        else
          rpcs.add("remove",
                  std::function<void (Address address)>(
                  [this, &rpcs] (Address address)
                    {
                      this->_require_auth(rpcs, true);
                      this->remove(address, {});
                    }));
        rpcs.add("ping",
                std::function<int(int)>(
                  [this] (int i)
                  {
                    return i;
                  }));
        auto stored_challenge = std::make_shared<elle::Buffer>();
        typedef std::pair<elle::Buffer, elle::Buffer> Challenge;

        auto auth_syn = [this, &rpcs, stored_challenge] (Passport const& p)
          -> std::pair<Challenge, Passport*>
          {
            ELLE_TRACE("%s: authentication syn", *this);
            bool verify = this->_doughnut.verify(p, false, false, false);
            if (!verify)
            {
              ELLE_LOG("Passport validation failed");
              throw elle::Error("Passport validation failed");
            }
            // generate and store a challenge to ensure remote owns the passport
            auto challenge = infinit::cryptography::random::generate<elle::Buffer>(128);
            *stored_challenge = std::move(challenge);
            this->_passports.insert(std::make_pair(&rpcs, p));
            return std::make_pair(
              std::make_pair(*stored_challenge, elle::Buffer()), // we no longuer need token
              const_cast<Passport*>(&_doughnut.passport()));
          };
        if (this->_doughnut.version() >= elle::Version(0, 7, 0))
        {
          using AuthSyn =
            Remote::Auth (Address id, Passport const&, elle::Version const&);
          rpcs.add(
            "auth_syn", std::function<AuthSyn>(
              [this, auth_syn]
              (Address id, Passport const& p, elle::Version const& v)
                -> Remote::Auth
              {
                if (this->doughnut().id() == id)
                  throw HandshakeFailed(
                    elle::sprintf("incoming peer has same id as us: %s", id));
                auto dht_v = this->_doughnut.version();
                if (v.major() != dht_v.major() || v.minor() != dht_v.minor())
                  throw HandshakeFailed(
                    elle::sprintf("invalid version %s, we use %s", v, dht_v));
                auto res = auth_syn(p);
                return Remote::Auth(
                  this->id(), std::move(res.first), *res.second);
              }));
        }
        else if (this->_doughnut.version() >= elle::Version(0, 4, 0))
        {
          typedef std::pair<Challenge, Passport*>
            AuthSyn(Passport const&, elle::Version const&);
          rpcs.add(
            "auth_syn", std::function<AuthSyn>(
              [this, auth_syn] (Passport const& p, elle::Version const& v)
                -> std::pair<Challenge, Passport*>
              {
                auto dht_v = this->_doughnut.version();
                if (v.major() != dht_v.major() || v.minor() != dht_v.minor())
                  elle::err("invalid version %s, we use %s", v, dht_v);
                return auth_syn(p);
              }));
        }
        else
        {
          typedef std::pair<Challenge, Passport*> AuthSyn(Passport const&);
          rpcs.add(
            "auth_syn", std::function<AuthSyn>(
              [this, auth_syn] (Passport const& p)
                -> std::pair<Challenge, Passport*>
              {
                return auth_syn(p);
              }));
        }
        rpcs.add("auth_ack", std::function<bool(elle::Buffer const&,
          elle::Buffer const&, elle::Buffer const&)>(
          [this, &rpcs, stored_challenge](
                 elle::Buffer const& enc_key,
                 elle::Buffer const& /*token*/,
                 elle::Buffer const& signed_challenge) -> bool
          {
            ELLE_TRACE("%s: authentication ack", this);
            if (stored_challenge->empty())
              throw elle::Error("auth_syn must be called before auth_ack");
            auto& passport = this->_passports.at(&rpcs);
            bool ok = passport.user().verify(
              signed_challenge,
              *stored_challenge,
              infinit::cryptography::rsa::Padding::pss,
              infinit::cryptography::Oneway::sha256);
            if (!ok)
            {
              ELLE_LOG("Challenge verification failed");
              throw elle::Error("Challenge verification failed");
            }
            elle::Buffer password = this->_doughnut.keys().k().open(
              enc_key,
              infinit::cryptography::Cipher::aes256,
              infinit::cryptography::Mode::cbc);
            rpcs._key.emplace(std::move(password));
            rpcs._ready(&rpcs);
            return true;
          }));
        using Keys = std::vector<cryptography::rsa::PublicKey>;
        rpcs.add(
          "resolve_keys",
          std::function<Keys (std::vector<int> const&)>(
            std::bind(&Local::_resolve_keys, this, std::placeholders::_1)));
        using KeysMap = std::unordered_map<int, cryptography::rsa::PublicKey>;
        rpcs.add(
          "resolve_all_keys",
          std::function<KeysMap()>(std::bind(&Local::_resolve_all_keys, this)));
      }

      void
      Local::_serve(std::function<std::unique_ptr<std::iostream> ()> accept)
      {
        static bool disable_key = elle::os::inenv("INFINIT_RPC_DISABLE_CRYPTO");
        elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
        {
          while (true)
          {
            auto socket = std::shared_ptr<std::iostream>(accept().release());
            auto name = elle::sprintf("%s: %s", this, socket);
            scope.run_background(
              name,
              [this, socket = std::move(socket)]
              {
                try
                {
                  auto conn = std::make_shared<Connection>(
                    *this, std::move(socket));
                  elle::SafeFinally remove;
                  // Don't make this connection visible until auth is done.
                  if (disable_key)
                  {
                    this->_peers.emplace_front(conn);
                    remove.action(
                      [this, it = this->_peers.begin()] ()
                      {
                        this->_peers.erase(it);
                      });
                  }
                  else
                    elle::unconst(conn->rpcs()._ready).connect(
                      [this, &conn, &remove] (RPCServer*)
                      {
                        this->_peers.emplace_front(conn);
                        remove.action(
                          [this, &conn, it = this->_peers.begin()] ()
                          {
                            elle::unconst(conn->rpcs()._ready).
                              disconnect_all_slots();
                            this->_peers.erase(it);
                          });
                      });
                  conn->_run();
                }
                catch (infinit::protocol::Serializer::EOF const&)
                {}
                catch (reactor::network::ConnectionClosed const& e)
                {}
                catch (reactor::network::SocketClosed const& e)
                {
                  ELLE_TRACE("unexpected SocketClosed: %s", e.backtrace());
                }
                catch (elle::Error const& e)
                {
                  ELLE_WARN("drop client %s: %s", socket, e);
                }
                elle::With<reactor::Thread::NonInterruptible>() << [&]
                {
                  ELLE_DEBUG("%s: RESETING", this);
                  elle::unconst(socket).reset();
                  ELLE_DEBUG("%s: RESETED", this);
                };
              });
          }
        };
      }

      void
      Local::_serve_tcp()
      {
        this->_serve([this] { return this->_server->accept(); });
      }

      void
      Local::_serve_utp()
      {
        this->_serve([this] { return this->_utp_server->accept(); });
      }

      /*-----------.
      | Connection |
      `-----------*/

      Local::Connection::Connection(Local& local,
                                    std::shared_ptr<std::iostream> stream)
        : _local(local)
        , _stream(std::move(stream))
        , _serializer(*this->_stream,
                      elle_serialization_version(local.doughnut().version()),
                      false)
        , _channels{this->_serializer}
        , _rpcs(this->_local.doughnut().version())
      {
        this->_local._register_rpcs(this->_rpcs);
        this->_local._on_connect(this->_rpcs);
        this->_rpcs.set_context<Doughnut*>(&this->_local.doughnut());
      }

      void
      Local::Connection::_run()
      {
        this->_rpcs.serve(this->_channels);
      }
    }
  }
}
