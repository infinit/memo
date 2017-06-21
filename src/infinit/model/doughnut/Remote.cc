#include <infinit/model/doughnut/Remote.hh>

#include <elle/bench.hh>
#include <elle/log.hh>
#include <elle/make-vector.hh>
#include <elle/os/environ.hh>
#include <elle/utils.hh>

#include <elle/reactor/Scope.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>

#include <infinit/RPC.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote")

#define BENCH(name)                                                     \
  static auto bench = elle::Bench{"bench.remote." name, std::chrono::seconds(10000)};     \
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

      /*-------------.
      | Construction |
      `-------------*/

      Remote::Remote(Doughnut& dht,
                     std::shared_ptr<Dock::Connection> connection)
        : Super(dht, connection->location().id())
        , _connecting_since(std::chrono::system_clock::now())
      {
        ELLE_TRACE_SCOPE("%s: construct", this);
        ELLE_ASSERT(connection->location().id());
        this->connection(std::move(connection));
        this->_connected.changed().connect(
          [this] (bool opened)
          {
            if (opened)
            {
              this->_disconnected_exception = {};
              if (!this->_connected.exception())
                this->Peer::connected()();
            }
            else
              this->Peer::disconnected()();
          });
      }

      void
      Remote::connection(std::shared_ptr<Dock::Connection> connection)
      {
        this->_connection = std::move(connection);
        this->_connections.clear();
        this->_connections.emplace_back(
          this->_connection->on_connection().connect(
            // Make sure we note this peer is connected before anything else
            // otherwise other slots trying to perform RPCs will deadlock
            // (e.g. Kouncil).
            boost::signals2::at_front,
            [this]
            {
              ELLE_TRACE("%s: connected", this);
              this->_connected.open();
            }));
        this->_connections.emplace_back(
          this->_connection->on_disconnection().connect(
            [this]
            {
              if (!this->_connection->connected())
              {
                ELLE_TRACE("%s: disconnected with exception %s",
                           this, elle::exception_string(
                             this->_connection->disconnected_exception()));
                this->_connected.raise(
                  this->_connection->disconnected_exception());
              }
              else
              {
                ELLE_TRACE_SCOPE("%s: disconnected", this);
                std::shared_ptr<Peer> hold;
                try
                {
                  hold = this->shared_from_this();
                }
                catch (std::bad_weak_ptr const&)
                {
                }
                this->_connected.close();
              }
            }));
        auto connected =
          this->_connection->connected() && !this->_connection->disconnected();
        if (!this->_connected && connected)
        {
          ELLE_TRACE("%s: connected", this);
          this->_connected.open();
        }
        else if (this->_connected && !connected)
        {
          ELLE_TRACE("%s: disconnected YYY", this);
          this->_connected.close();
        }
      }

      Remote::~Remote()
      {
        ELLE_TRACE_SCOPE("%s: destruct", this);
        this->_doughnut.dock()._peer_cache.erase(this->_cache_iterator);
        this->_cleanup();
      }

      void
      Remote::_cleanup()
      {
        this->_connection->disconnect();
      }

      Endpoints const&
      Remote::endpoints() const
      {
        return this->_connection->location().endpoints();
      }

      elle::Buffer const&
      Remote::credentials() const
      {
        return this->_connection->credentials();
      }

      void
      Remote::reconnect()
      {
        ELLE_TRACE("%s: reconnect, connected=%s", this, !!this->_connected);
        // It is possible for the whole code below the next line to do
        // absolutely nothing synchronously. In that case we still need to
        // reset the _connected exception to avoid busy-looping in safe_perform.
        this->_connected.clear_exception();
        this->_connection->disconnect();
        if (this->_connection->connected())
          this->_disconnected_exception =
            this->_connection->disconnected_exception();
        this->_connecting_since = std::chrono::system_clock::now();
        // If the dock decides to create a different Dock::Connection
        // because this->_connection is definitely broken, it will be
        // injected into this remote (tracked in dock's _peer_cache).
        this->_doughnut.dock().connect(this->_connection->location());
      }

      /*-----------.
      | Networking |
      `-----------*/

      void
      Remote::connect(elle::DurationOpt timeout)
      {
        if (!this->_connected)
        {
          if (this->_connection->disconnected())
          {
            ELLE_TRACE_SCOPE("%s: restart closed connection to %s",
                             this, this->_connection->location());
            this->connection(this->doughnut().dock().connect(
                               this->_connection->location()));
          }
          ELLE_DEBUG_SCOPE(
            "%s: wait for connection with timeout %s", this, timeout);
          if (!elle::reactor::wait(this->_connected, timeout))
            throw elle::reactor::network::TimeOut();
        }
      }

      void
      Remote::disconnect()
      {
        this->_connection->disconnect();
      }

      /*-------.
      | Blocks |
      `-------*/

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

      /*-----.
      | Keys |
      `-----*/

      std::vector<elle::cryptography::rsa::PublicKey>
      Remote::_resolve_keys(std::vector<int> const& ids)
      {
        static auto bench =
          elle::Bench{"bench.remote_key_cache_hit", std::chrono::seconds(1000)};
        {
          auto missing = std::vector<int>{};
          for (auto id: ids)
            if (this->key_hash_cache().get<1>().find(id) ==
                this->key_hash_cache().get<1>().end())
              missing.emplace_back(id);
          if (missing.size())
          {
            bench.add(0);
            ELLE_TRACE("%s: fetch %s keys by ids", this, missing.size());
            auto rpc = this->make_rpc<std::vector<elle::cryptography::rsa::PublicKey>(
              std::vector<int> const&)>("resolve_keys");
            auto missing_keys = rpc(missing);
            if (missing_keys.size() != missing.size())
              elle::err("resolve_keys for %s keys on %s gave %s replies",
                        missing.size(), this, missing_keys.size());
            auto id_it = missing.begin();
            auto key_it = missing_keys.begin();
            for (; id_it != missing.end(); ++id_it, ++key_it)
              this->key_hash_cache().emplace(*id_it, std::move(*key_it));
          }
          else
            bench.add(1);
        }
        return elle::make_vector(ids, [this] (auto id) {
            auto it = this->key_hash_cache().get<1>().find(id);
            ELLE_ASSERT(it != this->key_hash_cache().get<1>().end());
            return *it->key;
          });
      }

      std::unordered_map<int, elle::cryptography::rsa::PublicKey>
      Remote::_resolve_all_keys()
      {
        using Keys = std::unordered_map<int, elle::cryptography::rsa::PublicKey>;
        auto res = this->make_rpc<Keys()>("resolve_all_keys")();
        auto& kcache = this->key_hash_cache();
        for (auto const& key: res)
          if (kcache.get<1>().find(key.first) == kcache.get<1>().end())
            kcache.emplace(key.first, key.second);
        return res;
      }

      KeyCache const&
      Remote::key_hash_cache() const
      {
        return ELLE_ENFORCE(this->_connection)->key_hash_cache();
      }

      KeyCache&
      Remote::key_hash_cache()
      {
        return ELLE_ENFORCE(this->_connection)->key_hash_cache();
      }
    }
  }
}
