#include <infinit/model/doughnut/Local.hh>

#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/network/Interface.hh>
#include <elle/utility/Move.hh>

#include <cryptography/random.hh>
#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/rsa/Padding.hh>

#include <reactor/Scope.hh>

#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/MissingBlock.hh>
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

      Local::Local(Doughnut& dht,
                   Address id,
                   std::unique_ptr<storage::Storage> storage,
                   int port,
                   Protocol p)
        : Super(dht, std::move(id))
        , _storage(std::move(storage))
      {
        try
        {
          ELLE_TRACE_SCOPE("%s: construct", this);
          bool v6 = elle::os::getenv("INFINIT_NO_IPV6", "").empty()
              && dht.version() >= elle::Version(0, 7, 0);
          if (p == Protocol::tcp || p == Protocol::all)
          {
            this->_server = elle::make_unique<reactor::network::TCPServer>();
            this->_server->listen(port, v6);
            this->_server_thread = elle::make_unique<reactor::Thread>(
              elle::sprintf("%s server", *this),
              [this] { this->_serve_tcp(); });
          }
          if (p == Protocol::utp || p == Protocol::all)
          {
            this->_utp_server = elle::make_unique<reactor::network::UTPServer>();
            if (this->_server)
              port = this->_server->port();
            this->_utp_server->listen(port, v6);
            this->_utp_server_thread = elle::make_unique<reactor::Thread>(
              elle::sprintf("%s utp server", *this),
              [this] { this->_serve_utp(); });
          }
          ELLE_TRACE("%s: listen on %s", *this, this->server_endpoint());
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("%s: initialization failed with: %s", e.what());
          throw;
        }
      }

      Local::~Local()
      {
        ELLE_TRACE_SCOPE("%s: destruct", *this);
        if (this->_server_thread)
          this->_server_thread->terminate_now();
        if (this->_utp_server_thread)
          this->_utp_server_thread->terminate_now();
      }

      void
      Local::initialize()
      {}

      void
      Local::cleanup()
      {}

      /*-----------.
      | Networking |
      `-----------*/

      void
      Local::connect(elle::DurationOpt)
      {}

      void
      Local::reconnect(elle::DurationOpt)
      {}

      /*-------.
      | Blocks |
      `-------*/

      void
      Local::store(blocks::Block const& block, StoreMode mode)
      {
        ELLE_ASSERT(&block);
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        ELLE_DEBUG("%s: validate block", *this)
          if (auto res = block.validate(this->doughnut())); else
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
            if (auto* acb = dynamic_cast<const ACB*>(mblock))
            {
              auto v = acb->validate_admin_keys(this->doughnut());
              if (!v)
                throw ValidationFailed(v.reason());
            }
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
        elle::Buffer data;
        {
          elle::IOStream s(data.ostreambuf());
          Serializer::SerializerOut output(s);
          auto ptr = &block;
          output.serialize_forward(ptr);
        }
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

      /*-------.
      | Server |
      `-------*/

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

      std::vector<reactor::network::TCPServer::EndPoint>
      Local::server_endpoints()
      {
        bool v6 = elle::os::getenv("INFINIT_NO_IPV6", "").empty()
                  && this->doughnut().version() >= elle::Version(0, 7, 0);
        auto ep = this->server_endpoint();
        if (ep.address() != boost::asio::ip::address_v6::any()
         && ep.address() != boost::asio::ip::address_v4::any())
          return { ep };

        std::vector<reactor::network::TCPServer::EndPoint> res;
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
        if (this->_doughnut.version() >= elle::Version(0, 4, 0))
        {
          typedef std::pair<Challenge, Passport*>
            AuthSyn(Passport const&, elle::Version const&);
          rpcs.add(
            "auth_syn", std::function<AuthSyn>(
              [this, auth_syn] (Passport const& p, elle::Version const& v)
                -> std::pair<Challenge, Passport*>
              {
                auto dht_version = this->_doughnut.version();
                auto version =
                  elle::Version(dht_version.major(), dht_version.minor(), 0);
                if (v != version)
                  throw elle::Error(
                    elle::sprintf("invalid version %s, we use %s",
                                  v, this->_doughnut.version()));
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
            ELLE_TRACE("auth_ack, dn=%s", this->_doughnut);
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
            rpcs._key.reset(new infinit::cryptography::SecretKey(
              std::move(password)));
            return true;
          }));
      }

      void
      Local::_serve(std::function<std::unique_ptr<std::iostream> ()> accept)
      {
        elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
        {
          while (true)
          {
            auto socket = elle::utility::move_on_copy(accept());
            auto name = elle::sprintf("%s: %s server", *this, **socket);
            scope.run_background(
              name,
              [this, socket]
              {
                try
                {
                  RPCServer rpcs(this->_doughnut.version());
                  this->_register_rpcs(rpcs);
                  this->_on_connect(rpcs);
                  rpcs.set_context<Doughnut*>(&this->_doughnut);
                  rpcs.serve(**socket);
                }
                catch (elle::Error const& e)
                {
                  ELLE_WARN("drop client %s: %s", **socket, e);
                }
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
    }
  }
}
