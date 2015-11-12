#include <infinit/model/doughnut/Local.hh>

#include <elle/log.hh>
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
        : Super(std::move(id))
        , _storage(std::move(storage))
        , _doughnut(dht)
        , _rpcs(&dht)
      {
        if (p == Protocol::tcp || p == Protocol::all)
        {
          this->_server = elle::make_unique<reactor::network::TCPServer>();
          this->_server->listen(port);
          this->_server_thread = elle::make_unique<reactor::Thread>(
            elle::sprintf("%s server", *this),
            [this] { this->_serve_tcp(); });
        }
        if (p == Protocol::utp || p == Protocol::all)
        {
          this->_utp_server = elle::make_unique<reactor::network::UTPServer>();
          if (this->_server)
            port = this->_server->port();
          this->_utp_server->listen(port);
          this->_utp_server_thread = elle::make_unique<reactor::Thread>(
            elle::sprintf("%s utp server", *this),
            [this] { this->_serve_utp(); });
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
          if (auto res = block.validate()); else
            throw ValidationFailed(res.reason());
        if (auto *mblock = dynamic_cast<blocks::MutableBlock const*>(&block))
          try
          {
            auto previous_buffer = this->_storage->get(block.address());
            elle::IOStream s(previous_buffer.istreambuf());
            typename elle::serialization::binary::SerializerIn input(s);
            input.set_context<Doughnut*>(&this->_doughnut);
            auto previous = input.deserialize<std::unique_ptr<blocks::Block>>();
            auto mprevious =
              dynamic_cast<blocks::MutableBlock const*>(previous.get());
            if (!mprevious)
              throw ValidationFailed("overwriting a non-mutable block");
            if (mblock->version() <= mprevious->version())
              throw Conflict(
                elle::sprintf("version %s is not superior to current version %s",
                              mblock->version(), mprevious->version()));
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
        this->_storage->set(block.address(), data,
                            mode == STORE_ANY || mode == STORE_INSERT,
                            mode == STORE_ANY || mode == STORE_UPDATE);
        on_store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Local::fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
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
        on_fetch(address, res);
        return std::move(res);
      }

      void
      Local::remove(Address address)
      {
        ELLE_DEBUG("remove %x", address);
        try
        {
          this->_storage->erase(address);
        }
        catch (storage::MissingKey const& k)
        {
          throw MissingBlock(k.key());
        }
        on_remove(address);
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

      void
      Local::_register_rpcs(RPCServer& rpcs)
      {
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
        rpcs.add("ping",
                std::function<int(int)>(
                  [this] (int i)
                  {
                    return i;
                  }));
        typedef std::pair<elle::Buffer, elle::Buffer> Challenge;
        rpcs.add("auth_syn", std::function<std::pair<Challenge,Passport*>(Passport const&)>(
          [this] (Passport const& p) -> std::pair<Challenge, Passport*>
          {
            ELLE_TRACE("entering auth_syn, dn=%s", this->_doughnut);
            bool verify = const_cast<Passport&>(p).verify(this->_doughnut.owner());
            ELLE_TRACE("auth_syn verify = %s", verify);
            if (!verify)
            {
              ELLE_LOG("Passport validation failed");
              throw elle::Error("Passport validation failed");
            }
            // generate and store a challenge to ensure remote owns the passport
            auto challenge = infinit::cryptography::random::generate<elle::Buffer>(128);
            auto token = infinit::cryptography::random::generate<elle::Buffer>(128);
            this->_challenges.insert(std::make_pair(token.string(),
              std::make_pair(challenge, std::move(p))));
            return std::make_pair(
              std::make_pair(challenge, token),
              const_cast<Passport*>(&_doughnut.passport()));
          }));
        rpcs.add("auth_ack", std::function<bool(elle::Buffer const&,
          elle::Buffer const&, elle::Buffer const&)>(
          [this](elle::Buffer const& enc_key,
                 elle::Buffer const& token,
                 elle::Buffer const& signed_challenge) -> bool
          {
            ELLE_TRACE("auth_ack, dn=%s", this->_doughnut);
            auto it = this->_challenges.find(token.string());
            if (it == this->_challenges.end())
            {
              ELLE_LOG("Challenge token does not exist.");
              throw elle::Error("challenge token does not exist");
            }
            auto& stored_challenge = it->second.first;
            auto& peer_passport = it->second.second;
            bool ok = peer_passport.user().verify(
              signed_challenge,
              stored_challenge,
              infinit::cryptography::rsa::Padding::pss,
              infinit::cryptography::Oneway::sha256);
            this->_challenges.erase(it);
            if (!ok)
            {
              ELLE_LOG("Challenge verification failed");
              throw elle::Error("Challenge verification failed");
            }
            elle::Buffer password = this->_doughnut.keys().k().open(
              enc_key,
              infinit::cryptography::Cipher::aes256,
              infinit::cryptography::Mode::cbc);
            _rpcs._key.Get().reset(new infinit::cryptography::SecretKey(
              std::move(password)));
            ELLE_TRACE("auth_ack exiting");
            return true;
          }));
      }

      void
      Local::_serve(std::function<std::unique_ptr<std::iostream> ()> accept)
      {
        this->_register_rpcs(_rpcs);
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
                _rpcs.set_context<Doughnut*>(&this->_doughnut);
                _rpcs.serve(**socket);
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

      /*----------.
      | Printable |
      `----------*/

      void
      Local::print(std::ostream& stream) const
      {
        elle::fprintf(stream, "%s(%s)", elle::type_info(*this), this->id());
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
