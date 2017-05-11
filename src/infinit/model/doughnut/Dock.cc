#include <memory>

#include <infinit/model/doughnut/Dock.hh>

#include <elle/find.hh>
#include <elle/log.hh>
#include <elle/multi_index_container.hh>
#include <elle/network/Interface.hh>
#include <elle/os.hh>
#include <elle/range.hh>

#include <elle/reactor/network/utp-server.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/HandshakeFailed.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/Overlay.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Dock")

namespace
{
  bool const disable_key = getenv("INFINIT_RPC_DISABLE_CRYPTO");
  auto const ipv6_enabled = elle::os::getenv("INFINIT_NO_IPV6", "").empty();

  template <typename Action>
  void
  retry_forever(elle::Duration delay,
                elle::Duration max_delay,
                std::string const& action_name,
                Action action)
  {
    while (true)
    {
      try
      {
        action();
        return;
      }
      catch (elle::Exception const& e)
      {
        ELLE_WARN("%s: exception %s", action_name, e.what());
        delay = std::min(delay * 2, max_delay);
        elle::reactor::sleep(delay);
      }
    }
  }
}

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      Dock::Dock(Doughnut& doughnut,
                 Protocol protocol,
                 boost::optional<int> port,
                 boost::optional<boost::asio::ip::address> listen_address,
                 boost::optional<std::string> rdv_host,
                 boost::optional<std::chrono::milliseconds> tcp_heartbeat)
        : _doughnut(doughnut)
        , _tcp_heartbeat(tcp_heartbeat)
        , _local_utp_server(
          doughnut.local() ? nullptr : new elle::reactor::network::UTPServer)
        , _utp_server(doughnut.local() ?
                      *doughnut.local()->utp_server() :
                      *this->_local_utp_server)
      {
        ELLE_TRACE_SCOPE("%s: construct", this);
        ELLE_DEBUG("tcp heartbeat: %s", tcp_heartbeat);
        if (this->_local_utp_server)
        {
          bool v6 = ipv6_enabled
            && doughnut.version() >= elle::Version(0, 7, 0);
          if (listen_address)
            this->_local_utp_server->listen(*listen_address,
                                            port.value_or(0), v6);
          else
            this->_local_utp_server->listen(port.value_or(0), v6);
        }
        if (rdv_host)
        {
          auto const uid = elle::sprintf("%x", _doughnut.id());
          this->_rdv_connect_thread.reset(
            new elle::reactor::Thread(
              "rdv_connect",
              [this, uid, rdv_host]
              {
                // The remotes_server does not accept incoming connections,
                // it is used to connect Remotes
                retry_forever(
                  10_sec, 120_sec, "Dock RDV connect",
                  [&]
                  {
                    this->_utp_server.rdv_connect(uid, rdv_host.get(), 120_sec);
                  });
              }));
        }
      }

      Dock::Dock(Dock&& source)
        : _doughnut(source._doughnut)
        , _local_utp_server(std::move(source._local_utp_server))
        , _utp_server(source._utp_server)
      {}

      Dock::~Dock()
      {
        this->cleanup();
      }

      void
      Dock::disconnect()
      {
        ELLE_TRACE_SCOPE("%s: disconnect", this);
        {
          // First terminate thread asynchronoulsy, otherwise some of these
          // threads could initiate new connection while we terminate others.
          auto connecting_copy = this->connecting();
          for (auto& connecting: connecting_copy)
            if (auto c = connecting.lock())
              if (c->_thread)
                c->_thread->terminate();
          auto connected_copy = this->_connected;
          for (auto& connected: connected_copy)
            if (auto c = connected.lock())
              if (c->_thread)
                c->_thread->terminate();
          for (auto& connecting: connecting_copy)
            if (auto c = connecting.lock())
              c->disconnect();
          for (auto& connected: connected_copy)
            if (auto c = connected.lock())
              c->disconnect();
        }
        auto peers_copy = this->peer_cache();
        for (auto& peer: peers_copy)
          if (auto p = peer.lock())
            if (auto r = std::dynamic_pointer_cast<Remote>(p))
              r->disconnect();
      }

      void
      Dock::cleanup()
      {
        ELLE_TRACE_SCOPE("%s: cleanup", this);
        if (this->_rdv_connect_thread)
          this->_rdv_connect_thread->terminate_now();
        this->_rdv_connect_thread.reset();
        // Disconnects peers, holding them alive so they don't unregister from
        // _peer_cache while we iterate on it.
        {
          auto hold = elle::make_vector(this->_peer_cache,
                                        [] (auto peer)
                                        {
                                          auto p = ELLE_ENFORCE(peer.lock());
                                          p->cleanup();
                                          return std::move(p);
                                        });
          // Release all peer. All peers should have unregistered, otherwise
          // someone is still holding one alive and we are in trouble.
          hold.clear();
        }
        if (!this->_peer_cache.empty())
          ELLE_ERR("%s: some remotes are still alive", this)
          {
            for (auto wp: this->_peer_cache)
              if (auto p = wp.lock())
                ELLE_ERR("%s is held by %s references", p, p.use_count());
          }
        ELLE_ASSERT(this->_peer_cache.empty());
      }

      /*-----------.
      | Connection |
      `-----------*/

      Address const&
      Dock::Connection::id() const
      {
        return this->_location.id();
      }

      Endpoints const&
      Dock::Connection::endpoints() const
      {
        return this->_location.endpoints();
      }

      std::shared_ptr<Dock::Connection>
      Dock::connect(NodeLocation l, bool no_remote)
      {
        ELLE_TRACE_SCOPE("%s: connect to %f", this, l);
        // Check if we already have a connection to that peer.
        if (l.id() != Address::null)
        {
          if (auto i = elle::find(this->_connected, l.id()))
          {
            auto res = ELLE_ENFORCE(i->lock());
            ELLE_TRACE("%s: already connected %f: %s", this, l, res);
            return res;
          }
          else
            ELLE_TRACE("%s: not yet connected %f", this, l);
        }
        // Check if we are already connecting to that peer.
        {
          // FIXME: index by id + endpoints instead ?
          auto connecting
            = elle::as_range(this->_connecting.equal_range(l.endpoints()));
          for (auto wc: connecting)
          {
            auto c = ELLE_ENFORCE(wc.lock());
            if (c->id() == l.id())
            {
              ELLE_TRACE("already connecting to %f: %s", l, c);
              return c;
            }
          }
        }
        // Otherwise start connection.
        {
          auto connection = Connection::make(*this, std::move(l));
          ELLE_TRACE_SCOPE("initiate %s", connection);
          connection->init();
          if (!no_remote)
            connection->on_connection().connect(
              [this, connection] () mutable
              {
                this->doughnut().dock().make_peer(connection, true);
                // Delay termination from destructor.
                elle::With<elle::reactor::Thread::NonInterruptible>() << [&]
                {
                  connection.reset();
                };
              });
          return connection;
        }
      }

      Dock::Connection::Connection(
        Dock& dock,
        NodeLocation l)
        : _dock(dock)
        , _location(l)
        , _socket(nullptr)
        , _serializer()
        , _channels()
        , _rpc_server()
        , _credentials()
        , _thread()
        , _connected(false)
        , _disconnected(false)
        , _disconnected_since(std::chrono::system_clock::now())
        , _disconnected_exception()
        , _key_hash_cache()
      {}

      std::shared_ptr<Dock::Connection>
      Dock::Connection::make(Dock& dock, NodeLocation loc)
      {
        auto res = std::shared_ptr<Dock::Connection>(new Connection(dock, loc));
        res->_self = res;
        return res;
      }

      std::shared_ptr<Dock::Connection>
      Dock::Connection::shared_from_this()
      {
        return this->_self.lock();
      }

      void
      Dock::Connection::init()
      {
        // BMI iterators are not invalidated on insertion and deletion.
        auto connecting_it =
          this->_dock._connecting.emplace(this->shared_from_this()).first;
        this->_cleanup_on_disconnect = [this, connecting_it] {
          this->_dock._connecting.erase(connecting_it);
          this->_on_connection.disconnect_all_slots();
        };
        this->_thread.reset(
          new elle::reactor::Thread(
            elle::sprintf("%f", this),
            [this, connecting_it]
            {
              elle::SafeFinally remove_from_connecting([&] {
                  this->_dock._connecting.erase(connecting_it);
                  this->_thread->dispose(true);
                  this->_thread.release();
                  this->_on_connection.disconnect_all_slots();
                });
              this->_cleanup_on_disconnect = std::function<void()>();
              bool connected = false;
              ELLE_TRACE_SCOPE("%f: connection attempt to %s endpoints",
                               this, this->_location.endpoints().size());
              ELLE_DEBUG("endpoints: %s", this->_location.endpoints());
              auto handshake =
                [&] (std::unique_ptr<std::iostream> socket,
                     boost::optional<std::chrono::milliseconds> ping)
                {
                  ELLE_TRACE("%f: handshake: %s", this, socket);
                  auto sv = elle_serialization_version(
                    this->_dock.doughnut().version());
                  auto serializer =
                    elle::make_unique<elle::protocol::Serializer>(
                      *socket, sv, false,
                      ping,
                      ping);
                  auto channels =
                    elle::make_unique<elle::protocol::ChanneledStream>(*serializer);
                  try
                  {
                    if (!disable_key)
                      this->_key_exchange(*channels);
                    ELLE_TRACE("%f: connected", this);
                    this->_socket = std::move(socket);
                    this->_serializer = std::move(serializer);
                    this->_channels = std::move(channels);
                    ELLE_ASSERT(this->_channels);
                    connected = true;
                  }
                  catch (...)
                  {
                    ELLE_TRACE("%f: handshake: %s: caught: %s",
                               this, socket, elle::exception_string());
                    // Delay termination from destructor.
                    elle::With<elle::reactor::Thread::NonInterruptible>() << [&]
                    {
                      channels.reset();
                    };
                    throw;
                  }
                };
              auto umbrella = [&, this] (auto const& f)
                {
                  return [f, this]
                  {
                    try
                    {
                      f();
                    }
                    catch (elle::reactor::network::Error const& e)
                    {
                      ELLE_DEBUG("%f: endpoint network failure: %s", this, e);
                    }
                    catch (HandshakeFailed const& hs)
                    {
                      ELLE_WARN("%f: endpoint handshake failure: %s", this, hs);
                    }
                  };
                };
              elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& scope)
              {
                if (this->_dock.doughnut().protocol().with_tcp())
                  for (auto const& e: this->_location.endpoints())
                  {
                    ELLE_TRACE("trying to connect to %s", e);
                    scope.run_background(
                      elle::sprintf("%s: tcp://%s",
                                    elle::reactor::scheduler().current()->name(), e),
                      umbrella(
                        [&, e]
                        {
                          using elle::reactor::network::TCPSocket;
                          handshake(elle::make_unique<TCPSocket>(e.tcp()),
                                    this->_dock._tcp_heartbeat);
                          this->_serializer->ping_timeout().connect(
                            [this]
                            {
                              ELLE_WARN("%s: heartbeat timeout", this);
                              this->_disconnected_exception =
                                std::make_exception_ptr(
                                  elle::reactor::network::ConnectionClosed());
                              this->_thread->terminate();
                            });
                          this->_connected_endpoint = e;
                          scope.terminate_now();
                        }));
                  }
                if (this->_dock.doughnut().protocol().with_utp())
                  scope.run_background(
                    elle::sprintf("%s: utp://%s",
                                  this, this->_location.endpoints()),
                    umbrella(
                      [&, eps = this->_location.endpoints().udp()]
                      {
                        std::string cid;
                        if (this->_location.id() != Address::null)
                          cid = elle::sprintf("%x", this->_location.id());
                        auto socket =
                          elle::make_unique<elle::reactor::network::UTPSocket>(
                            this->_dock._utp_server);
                        this->_connected_endpoint = socket->peer();
                        socket->connect(cid, eps);
                        handshake(std::move(socket), {});
                        scope.terminate_now();
                      }));
                elle::reactor::wait(scope);
              };
              remove_from_connecting.abort();
              if (!connected)
              {
                this->_disconnected = true;
                this->_disconnected_since = std::chrono::system_clock::now();
                // FIXME: keep a better exception, for instance if the passport
                // failed to validate etc.
                this->_disconnected_exception = std::make_exception_ptr(
                  elle::reactor::network::ConnectionRefused());
                ELLE_TRACE("%s: connection to %f failed",
                           this, this->_location.endpoints());
                this->_dock._connecting.erase(connecting_it);
                auto hold = this->shared_from_this();
                this->_thread->dispose(true);
                this->_thread.release();
                this->_on_connection.disconnect_all_slots();
                this->_on_disconnection();
                return;
              }
              // Check for duplicates.
              auto id = this->_location.id();
              ELLE_ASSERT_NEQ(id, Address::null);
              if (elle::contains(this->_dock._connected, id))
              {
                ELLE_TRACE("%s: drop duplicate", this);
                this->_disconnected = true;
                this->_dock._connecting.erase(connecting_it);
                auto hold = this->shared_from_this();
                this->_thread->dispose(true);
                this->_thread.release();
                this->_on_connection.disconnect_all_slots();
                this->_on_disconnection.disconnect_all_slots();
                return;
              }
              this->_connected = true;
              ELLE_TRACE("connected through %s", this->_connected_endpoint);
              this->_connected_it =
                this->_dock._connected.insert(this->shared_from_this()).first;
              this->_dock._connecting.erase(connecting_it);
              auto const cleanup = [&]
                {
                  if (this->_connected_it)
                  {
                    this->_dock._connected.erase(this->_connected_it.get());
                    this->_connected_it.reset();
                  }
                  this->_disconnected = true;
                  this->_disconnected_since = std::chrono::system_clock::now();
                  {
                    auto hold = this->shared_from_this();
                    this->_on_connection.disconnect_all_slots();
                    this->_on_disconnection();
                    this->_on_disconnection.disconnect_all_slots();
                    if (hold.use_count() == 1)
                      this->_thread.release()->dispose(true);
                  }
                };
              try
              {
                this->_dock.on_connection()(*this);
                ELLE_DEBUG("invoke connected hook")
                {
                  auto hold = this->shared_from_this();
                  {
                    elle::SafeFinally sf([&] {
                      this->_on_connection.disconnect_all_slots();
                    });
                    this->_on_connection();
                  }
                  if (hold.use_count() == 1)
                  {
                    this->_thread.release()->dispose(true);
                    return;
                  }
                }
                ELLE_ASSERT(this->_channels);
                ELLE_TRACE("serve RPCs")
                  this->_rpc_server.serve(*this->_channels);
                ELLE_TRACE("connection ended");
                this->_disconnected_exception =
                  std::make_exception_ptr(elle::reactor::network::ConnectionClosed());
              }
              catch (elle::reactor::network::Error const& e)
              {
                ELLE_TRACE("connection fell: %s", e);
                this->_disconnected_exception = std::current_exception();
              }
              catch (...)
              {
                cleanup();
                throw;
              }
              cleanup();
            }));
      }

      Dock::Connection::~Connection() noexcept(false)
      {
        if (this->_connected_it)
        {
          this->_dock._connected.erase(this->_connected_it.get());
          this->_connected_it.reset();
        }
        // Delay termination from destructor.
        elle::With<elle::reactor::Thread::NonInterruptible>() << [&]
        {
          if (this->_thread)
            this->_thread->terminate_now(false);
          this->_channels.reset();
        };
      }

      void
      Dock::Connection::disconnect()
      {
        if (!this->_disconnected && this->_thread && !this->_thread->done())
        {
          ELLE_TRACE_SCOPE("%s: disconnect", this);
          // Do not suicide we might be unwinding.
          this->_thread->terminate_now(false);
          if (this->_cleanup_on_disconnect)
            this->_cleanup_on_disconnect();
        }
      }

      void
      Dock::Connection::print(std::ostream& out) const
      {
        elle::fprintf(out, "%f(%x, %f -> %f)",
                      elle::type_info(*this),
                      reinterpret_cast<void const*>(this),
                      this->_dock.doughnut().id(),
                      this->_location.id());
      }

      namespace
      {
        std::pair<Remote::Challenge, std::unique_ptr<Passport>>
        _auth_0_3(Dock::Connection& self, elle::protocol::ChanneledStream& channels)
        {
          using AuthSyn = std::pair<Remote::Challenge, std::unique_ptr<Passport>>
            (Passport const&);
          RPC<AuthSyn> auth_syn(
            "auth_syn", channels, self.dock().doughnut().version());
          return auth_syn(self.dock().doughnut().passport());
        }

        std::pair<Remote::Challenge, std::unique_ptr<Passport>>
        _auth_0_4(Dock::Connection& self, elle::protocol::ChanneledStream& channels)
        {
          using AuthSyn = std::pair<Remote::Challenge, std::unique_ptr<Passport>>
            (Passport const&, elle::Version const&);
          RPC<AuthSyn> auth_syn(
            "auth_syn", channels, self.dock().doughnut().version());
          auth_syn.set_context<Doughnut*>(&self.dock().doughnut());
          auto version = self.dock().doughnut().version();
          // 0.5.0 and 0.6.0 compares the full version it receives for
          // compatibility instead of dropping the subminor component. Set
          // it to 0.
          return auth_syn(
            self.dock().doughnut().passport(),
            elle::Version(version.major(), version.minor(), 0));
        }

        Remote::Auth
        _auth_0_7(Dock::Connection& self, elle::protocol::ChanneledStream& channels)
        {
          using AuthSyn =
            Remote::Auth (Address, Passport const&, elle::Version const&);
          RPC<AuthSyn> auth_syn(
            "auth_syn", channels, self.dock().doughnut().version());
          auth_syn.set_context<Doughnut*>(&self.dock().doughnut());
          auto res = auth_syn(self.dock().doughnut().id(),
                              self.dock().doughnut().passport(),
                              self.dock().doughnut().version());
          if (res.id == self.dock().doughnut().id())
            throw HandshakeFailed(elle::sprintf("peer has same id as us: %s",
                                                res.id));
          if (self.location().id() != Address::null &&
              self.location().id() != res.id)
            throw HandshakeFailed(
              elle::sprintf("peer id mismatch: expected %s, got %s",
                            self.location().id(), res.id));
          return res;
        }
      }

      void
      Dock::Connection::_key_exchange(elle::protocol::ChanneledStream& channels)
      {
        ELLE_TRACE_SCOPE("%s: exchange keys", *this);
        auto version = this->_dock.doughnut().version();
        auto& dht = this->_dock.doughnut();
        try
        {
          auto challenge_passport = [&]
          {
            if (version >= elle::Version(0, 7, 0))
            {
              auto res = _auth_0_7(*this, channels);
              if (this->_location.id() == model::Address::null)
                this->_location.id(res.id);
              return std::make_pair(
                res.challenge,
                elle::make_unique<Passport>(std::move(res.passport)));
            }
            else if (version >= elle::Version(0, 4, 0))
              return _auth_0_4(*this, channels);
            else
              return _auth_0_3(*this, channels);
          }();
          auto& remote_passport = challenge_passport.second;
          ELLE_ASSERT(remote_passport);
          if (!dht.verify(*remote_passport, false, false, false))
          {
            auto msg = elle::sprintf(
              "passport validation failed for %s", this->_location.id());
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
          auto signed_challenge = dht.keys().k().sign(
            challenge_passport.first.first,
            elle::cryptography::rsa::Padding::pss,
            elle::cryptography::Oneway::sha256);
          // generate, seal
          // dont set _key yet so that our 2 rpcs are in cleartext
          auto key = elle::cryptography::secretkey::generate(256);
          elle::Buffer password = key.password();
          auto sealed_key =
            remote_passport->user().seal(password,
                                         elle::cryptography::Cipher::aes256,
                                         elle::cryptography::Mode::cbc);
          ELLE_DEBUG("acknowledge authentication")
          {
            RPC<bool (elle::Buffer const&,
                      elle::Buffer const&,
                      elle::Buffer const&)>
              auth_ack("auth_ack", channels, version, nullptr);
            auth_ack(sealed_key,
                     challenge_passport.first.second,
                     signed_challenge);
            if (this->dock().doughnut().encrypt_options().encrypt_rpc)
            {
              this->_rpc_server._key.emplace(key);
              this->_credentials = std::move(password);
            }
          }
        }
        catch (elle::Error& e)
        {
          ELLE_WARN("key exchange failed with %s: %s",
                    this->_location.id(), elle::exception_string());
          throw;
        }
      }

      /*-----.
      | Peer |
      `-----*/

      overlay::Overlay::WeakMember
      Dock::make_peer(NodeLocation loc)
      {
        ELLE_TRACE_SCOPE("%s: get %f", this, loc);
        ELLE_ASSERT(loc.id() != Address::null);
        if (loc.id() == this->_doughnut.id())
        {
          ELLE_TRACE("peer is ourself");
          return this->_doughnut.local();
        }
        {
          auto it = this->_peer_cache.find(loc.id());
          if (it != this->_peer_cache.end())
            return overlay::Overlay::WeakMember::own(ELLE_ENFORCE(it->lock()));
        }
        return overlay::Overlay::WeakMember::own(
          this->make_peer(this->connect(loc)));
      }

      std::shared_ptr<Remote>
      Dock::make_peer(std::shared_ptr<Connection> connection, bool ignored_result)
      {
        {
          auto it = this->_peer_cache.find(connection->id());
          if (it != this->_peer_cache.end())
          {
            auto peer = ELLE_ENFORCE(it->lock());
            auto remote = ELLE_ENFORCE(std::dynamic_pointer_cast<Remote>(peer));
            if (remote->connection() == connection)
              ;
            // FIXME: This is messy. We could miss that current connection is
            // broken and not replace it, but we don't want to replace a working
            // connection with the new one and kill all RPCs.
            else if (!remote->connection()->connected() ||
                     remote->connection()->disconnected())
            {
              ELLE_TRACE_SCOPE(
                "%s: replace broken connection for %s", this, remote);
              remote->connection(connection);
            }
            else
              ELLE_TRACE("%s: drop duplicate %s", this, connection);
            return remote;
          }
        }
        // FIXME: don't always spawn paxos
        if (ignored_result && this->_on_peer.empty())
          return std::shared_ptr<Remote>();
        using RemotePeer = consensus::Paxos::RemotePeer;
        auto peer = std::make_shared<RemotePeer>(this->_doughnut, connection);
        auto insertion = this->_peer_cache.emplace(peer);
        ELLE_ASSERT(insertion.second);
        peer->_cache_iterator = insertion.first;
        this->_on_peer(peer);
        return peer;
      }
    }
  }
}
