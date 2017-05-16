#include <infinit/model/doughnut/Doughnut.hh>

#include <boost/optional.hpp>

#include <elle/Buffer.hh>
#include <elle/Error.hh>
#include <elle/IOStream.hh>
#include <elle/cast.hh>
#include <elle/format/hexadecimal.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <elle/cryptography/hash.hh>

#include <elle/reactor/Scope.hh>
#include <elle/reactor/exception.hh>
#include <elle/reactor/network/utp-server.hh>
#ifndef INFINIT_WINDOWS
# include <elle/reactor/network/unix-domain-server.hh>
# include <elle/reactor/network/unix-domain-socket.hh>
#endif

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/GB.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/model/doughnut/Consensus.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/model/doughnut/conflict/UBUpserter.hh>
#include <infinit/model/MonitoringServer.hh>
#include <infinit/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

namespace
{
#if INFINIT_ENABLE_PROMETHEUS
  /// A gauge to track the number of members of a DHT.
  ///
  /// May return nullptr if set up failed.
  infinit::prometheus::GaugePtr
  make_member_gauge(infinit::model::doughnut::Doughnut const& dht)
  {
    /// The family of counters used to count the number of
    /// connected members for this DHT.
    static auto* family
      = infinit::prometheus::instance().make_gauge_family(
          "infinit_overlay_peers",
          "How many overlay peers this overlay member is connected to");
    return infinit::prometheus::instance()
      .make(family, {{"id", elle::sprintf("%f", dht.id())}});
  }

  /// A gauge to track the number of blocks in a silo.
  ///
  /// @return nullptr if set up failed.
  infinit::prometheus::GaugePtr
  make_storage_blocks_gauge(infinit::model::doughnut::Doughnut const& dht)
  {
    static auto* family
      = infinit::prometheus::instance().make_gauge_family(
          "infinit_storage_blocks",
          "How many blocks this storage uses");
    if (dht.local())
      return infinit::prometheus::instance()
        .make(family,
            {
              {"id", elle::sprintf("%f", dht.id())},
              {"type", dht.local()->storage()->type()},
            });
    else
      return nullptr;
  }

  /// A gauge to track the number of bytes in a silo.
  ///
  /// @return nullptr if set up failed.
  infinit::prometheus::GaugePtr
  make_storage_bytes_gauge(infinit::model::doughnut::Doughnut const& dht)
  {
    static auto* family
      = infinit::prometheus::instance().make_gauge_family(
          "infinit_storage_bytes",
          "How many bytes this storage uses");
    if (dht.local())
      return infinit::prometheus::instance()
        .make(family,
            {
              {"id", elle::sprintf("%f", dht.id())},
              {"type", dht.local()->storage()->type()},
            });
    else
      return nullptr;
  }
#endif

  std::chrono::milliseconds
  _connect_timeout_val(elle::Defaulted<std::chrono::milliseconds> arg)
  {
    static auto const env = elle::os::getenv("INFINIT_CONNECT_TIMEOUT", "");
    if (arg || env.empty())
      return arg.get();
    else
      return elle::chrono::duration_parse<std::milli>(env);
  }

  std::chrono::milliseconds
  _soft_fail_timeout_val(elle::Defaulted<std::chrono::milliseconds> arg)
  {
    static auto const env = elle::os::getenv("INFINIT_SOFTFAIL_TIMEOUT", "");
    if (arg || env.empty())
      return arg.get();
    else
      return elle::chrono::duration_parse<std::milli>(env);
  }

  bool
  _soft_fail_running_val(elle::Defaulted<bool> arg)
  {
    static auto const inenv = elle::os::inenv("INFINIT_SOFTFAIL_RUNNING");
    if (inenv)
    {
      static auto const env = elle::os::getenv("INFINIT_SOFTFAIL_RUNNING");
      if (arg || env.empty())
        return arg.get();
      else
        return true; // FIXME: parse that value
    }
    else
      return arg.get();
  }

  const auto _dht_ser
    = elle::serialization::Hierarchy<infinit::model::ModelConfig>
    ::Register<infinit::model::doughnut::Configuration>("doughnut");

  const auto _dht_abbr
    = elle::TypeInfo::RegisterAbbrevation("infinit::model::doughnut", "dht");
}

namespace infinit
{
  namespace bfs = boost::filesystem;

  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      template <typename ConflictResolver,
                typename F,
                typename FF,
                typename... Args>
      void check_push(Doughnut& d,
                      std::string const& what,
                      Address where,
                      F (UB::*checker)(void) const,
                      FF reference_value,
                      Args... create)
      {
        while (true)
          try
          {
            ELLE_TRACE_SCOPE("%s: check %s", d, what);
            auto block = d.fetch(where);
            ELLE_DEBUG("%s: %s already present at %x",
                       d, what, block->address());
            auto const ub = elle::cast<UB>::runtime(block);
            auto const value = ((ub.get())->*checker)();
            if (value != reference_value)
              elle::err("%s: %s exists but differs at %x: %s != %s",
                        d, what, where, value, reference_value);
            break;
          }
          catch (MissingBlock const&)
          {
            auto user = std::make_unique<UB>(create...);
            ELLE_TRACE_SCOPE("%s: store %s at %f",
              d, what, user->address());
            try
            {
              d.insert(std::move(user),
                       std::make_unique<ConflictResolver>(what));
            }
            catch (elle::Error const& e)
            {
              ELLE_TRACE("%s: failed to store %s: %s", d, what, e);
              if (d.terminating().opened())
                break;
              elle::reactor::wait(d.terminating(), 1_sec);
            }
          }
      }

      static
      Protocol
      deprecate_utp(Protocol const& p)
      {
        bool utp_guard = std::stoi(elle::os::getenv("INFINIT_UTP", "0"));
        if (!utp_guard && p == Protocol::all)
        {
          ELLE_WARN(
            "Not using UTP although network configuration selects 'all' "
            "protocols. UTP has been temporarily deprecated. Force with "
            "INFINIT_UTP=1.");
          return Protocol::tcp;
        }
        else
          return p;
      }

      Doughnut::Doughnut(Init init)
        : Model(std::move(init.version))
        , _connect_timeout(
          _connect_timeout_val(std::move(init.connect_timeout)))
        , _soft_fail_timeout(
          _soft_fail_timeout_val(std::move(init.soft_fail_timeout)))
        , _soft_fail_running(
          _soft_fail_running_val(std::move(init.soft_fail_running)))
        , _id(std::move(init.id))
        , _protocol(deprecate_utp(init.protocol))
        , _resign_on_shutdown(init.resign_on_shutdown)
        , _keys(std::move(init.keys))
        , _owner(std::move(init.owner))
        , _passport(std::move(init.passport))
        , _admin_keys(std::move(init.admin_keys))
        , _encrypt_options(std::move(init.encrypt_options))
        , _consensus(init.consensus_builder(*this))
        , _local(
          init.storage
          ? this->_consensus->make_local(
            init.port,
            init.listen_address,
            std::move(init.storage))
          : nullptr)
        , _dock(*this,
                init.protocol,
                init.port,
                init.listen_address,
                std::move(init.rdv_host),
                init.tcp_heartbeat)
#if INFINIT_ENABLE_PROMETHEUS
          // To initialize before _overlay, which uses it.
        , _member_gauge{make_member_gauge(*this)}
          // To initialize after _local, it uses it.
        , _blocks_gauge{make_storage_blocks_gauge(*this)}
        , _bytes_gauge{make_storage_bytes_gauge(*this)}
#endif
        , _overlay(init.overlay_builder(*this, this->_local))
        , _pool([this] { return std::make_unique<ACB>(this); }, 100, 1)
        , _terminating()
      {
        if (this->_local)
        {
          this->_local->initialize();
#if INFINIT_ENABLE_PROMETHEUS
          if (auto* storage = this->_local->storage().get())
            if (this->_blocks_gauge.get())
            {
              storage->register_notifier([this, storage]
              {
                this->_blocks_gauge.get()->Set(storage->block_count());
                this->_bytes_gauge.get()->Set(storage->usage());
              });
            }
#endif
        }
        if (init.name)
        {
          auto check_user_blocks = [name = init.name.get(), this]
            {
              check_push<UserBlockUpserter>(*this,
                elle::sprintf("user block for %s", name),
                UB::hash_address(name, *this),
                &UB::key,
                this->keys().K(),
                this, name, this->passport());
              check_push<ReverseUserBlockUpserter>(*this,
                elle::sprintf("reverse user block for %s", name),
                UB::hash_address(this->keys().K(), *this),
                &UB::name,
                name,
                this, name, this->passport(), true);
              auto const hash = UB::hash(this->keys().K());
              check_push<UserBlockUpserter>(*this,
                elle::sprintf("key hash block for %s", name),
                UB::hash_address(':' + hash.string(), *this),
                &UB::key,
                this->keys().K(),
                this, ':' + hash.string(), this->keys().K());
            };
          this->_user_init.reset(
            new elle::reactor::Thread(
              elle::sprintf("%s: user blocks checker", *this),
              check_user_blocks));
        }
#ifndef INFINIT_WINDOWS
        if (init.monitoring_socket_path)
        {
          auto const& m_path = init.monitoring_socket_path.get();
          if (bfs::exists(m_path))
          {
            try
            {
              elle::reactor::network::UnixDomainSocket socket(m_path);
              ELLE_WARN(
                "unable to monitor, socket already present at: %s", m_path);
            }
            catch (elle::Exception const&)
            {
              bfs::remove(m_path);
            }
          }
          if (!bfs::exists(m_path))
          {
            try
            {
              auto unix_domain_server =
                std::make_unique<elle::reactor::network::UnixDomainServer>();
              if (!bfs::exists(m_path.parent_path()))
                bfs::create_directories(m_path.parent_path());
              unix_domain_server->listen(m_path);
              this->_monitoring_server.reset(
                new MonitoringServer(std::move(unix_domain_server), *this));
              ELLE_DEBUG("monitoring server listening on %s", m_path);
            }
            catch (elle::reactor::network::PermissionDenied const& e)
            {
              ELLE_WARN("unable to monitor, check "
                        "INFINIT_RUNTIME_DIR or XDG_RUNTIME_DIR");
              throw;
            }
            catch (elle::reactor::network::InvalidEndpoint const& e)
            {
              ELLE_WARN("unable to monitor, check "
                        "INFINIT_RUNTIME_DIR or XDG_RUNTIME_DIR");
              throw;
            }
          }
        }
#endif
      }

      Doughnut::~Doughnut()
      {
        ELLE_TRACE_SCOPE("%s: destruct", this);
        this->_terminating.open();
        if (this->_resign_on_shutdown)
        {
          ELLE_TRACE_SCOPE("resign");
          this->overlay()->storing(false);
          this->consensus()->resign();
        }
        if (this->_user_init)
        {
          if (!elle::reactor::wait(*this->_user_init, 5_sec))
            this->_user_init->terminate_now();
          this->_user_init.reset();
        }
        if (this->_local)
          this->_local->cleanup();
        if (this->_overlay)
          this->_overlay->cleanup();
        this->_consensus.reset();
        this->_dock.disconnect();
        this->_overlay.reset();
        this->_dock.cleanup();
        if (this->_local)
        {
          if (!this->_local.unique())
          {
            ELLE_ABORT("Doughnut destroyed with %s extra references to Local",
                       this->_local.use_count() - 1);
          }
          this->_local.reset();
        }
      }

      void
      Doughnut::resign()
      {
        this->consensus()->resign();
      }

      /*-----.
      | Time |
      `-----*/

      std::chrono::high_resolution_clock::time_point
      Doughnut::now()
      {
        return std::chrono::high_resolution_clock::now();
      }

      elle::cryptography::rsa::KeyPair const&
      Doughnut::keys() const
      {
        return *this->_keys;
      }

      std::shared_ptr<elle::cryptography::rsa::KeyPair>
      Doughnut::keys_shared() const
      {
        return this->_keys;
      }

      std::unique_ptr<blocks::MutableBlock>
      Doughnut::_make_mutable_block() const
      {
        ELLE_TRACE_SCOPE("%s: create OKB", *this);
        return std::make_unique<OKB>(elle::unconst(this));
      }

      std::unique_ptr<blocks::ImmutableBlock>
      Doughnut::_make_immutable_block(elle::Buffer content, Address owner) const
      {
        ELLE_TRACE_SCOPE("%s: create CHB", *this);
        return std::make_unique<CHB>(elle::unconst(this),
                                      std::move(content), owner);
      }

      std::unique_ptr<blocks::ACLBlock>
      Doughnut::_make_acl_block() const
      {
        ELLE_TRACE_SCOPE("%s: create ACB", *this);
        return elle::cast<blocks::ACLBlock>::runtime(
          elle::unconst(this)->_pool.get());
      }

      std::unique_ptr<blocks::GroupBlock>
      Doughnut::_make_group_block() const
      {
        return std::make_unique<GB>(
          elle::unconst(this),
          elle::cryptography::rsa::keypair::generate(2048));
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_make_named_block(elle::Buffer const& key) const
      {
        return std::make_unique<model::doughnut::NB>(
            elle::unconst(*this), key.string(), elle::Buffer());
      }

      Address
      Doughnut::_named_block_address(elle::Buffer const& key) const
      {
        return NB::address(this->keys().K(), key.string(), version());
      }

      std::unique_ptr<model::User>
      Doughnut::_make_user(elle::Buffer const& data) const
      {
        if (data.empty())
          elle::err("invalid empty user");
        else if (data[0] == '{')
        {
          ELLE_TRACE_SCOPE("%s: fetch user from public key", *this);
          elle::IOStream input(data.istreambuf());
          elle::serialization::json::SerializerIn s(input);
          elle::cryptography::rsa::PublicKey pub(s);
          try
          {
            auto block = this->fetch(UB::hash_address(pub, *this));
            auto ub = elle::cast<UB>::runtime(block);
            return std::make_unique<doughnut::User>(ub->key(), ub->name());
          }
          catch (MissingBlock const&)
          {
            ELLE_TRACE("Reverse UB not found, returning public key hash");
            auto hash = short_key_hash(pub);
            return std::make_unique<doughnut::User>(pub, hash);
          }
        }
        else if (data[0] == '@')
        {
          ELLE_TRACE_SCOPE("%s: fetch user from group", *this);
          auto gn = data.string().substr(1);
          Group g(*elle::unconst(this), gn);
          return std::make_unique<doughnut::User>(g.public_control_key(),
                                                   data.string());
        }
        else
        {
          ELLE_TRACE_SCOPE("%s: fetch user from name", *this);
          try
          {
            auto block = this->fetch(UB::hash_address(data.string(), *this));
            auto ub = elle::cast<UB>::runtime(block);
            return std::make_unique<doughnut::User>(ub->key(), data.string());
          }
          catch (infinit::model::MissingBlock const&)
          {
            return nullptr;
          }
        }
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address,
                       boost::optional<int> local_version) const
      {
        return this->_consensus->fetch(address, std::move(local_version));
      }

      void
      Doughnut::_fetch(std::vector<AddressVersion> const& addresses,
                       ReceiveBlock res) const
      {
        this->_consensus->fetch(addresses, res);
      }

      void
      Doughnut::_insert(std::unique_ptr<blocks::Block> block,
                        std::unique_ptr<ConflictResolver> resolver)
      {
        this->_consensus->store(std::move(block),
                                StoreMode::STORE_INSERT,
                                std::move(resolver));
      }

      void
      Doughnut::_update(std::unique_ptr<blocks::Block> block,
                        std::unique_ptr<ConflictResolver> resolver)
      {
        this->_consensus->store(std::move(block),
                                StoreMode::STORE_UPDATE,
                                std::move(resolver));
      }

      void
      Doughnut::_remove(Address address, blocks::RemoveSignature rs)
      {
        this->_consensus->remove(address, std::move(rs));
      }

      /*------------------.
      | Service discovery |
      `------------------*/

      Doughnut::ServicesTypes
      Doughnut::services()
      {
        if (auto block = this->_services_block(false))
          return elle::serialization::binary::deserialize
            <ServicesTypes>(block->data());
        else
          return Doughnut::ServicesTypes();
      }

      void
      Doughnut::service_add(std::string const& type,
                            std::string const& name,
                            elle::Buffer value)
      {
        if (this->keys().K() != *this->owner())
          elle::err("only the network owner may register services");
        auto block = this->_services_block(true);
        auto discovery = elle::serialization::binary::deserialize
          <ServicesTypes>(block->data());
        auto services = discovery.find(type);
        if (services == discovery.end())
          services = discovery.emplace(type, Services()).first;
        if (services->second.find(name) != services->second.end())
          elle::err("%s already registered: %s", type, name);
        auto vblock = this->make_block<blocks::ImmutableBlock>(
          std::move(value));
        auto vaddr = vblock->address();
        this->insert(std::move(vblock));
        services->second.emplace(name, vaddr);
        block->data(elle::serialization::binary::serialize(discovery));
        this->update(std::move(block));
      }

      std::unique_ptr<blocks::MutableBlock>
      Doughnut::_services_block(bool write)
      {
        try
        {
          auto beacon = this->fetch(
            NB::address(
              *this->owner(), "infinit/services", elle::Version(0, 7, 0)));
          auto addr = elle::serialization::binary::deserialize<Address>(
            beacon->data());
          try
          {
            return std::dynamic_pointer_cast<blocks::MutableBlock>(
              this->fetch(addr));
          }
          catch (MissingBlock const&)
          {
            elle::err("missing services block at %f", addr);
          }
        }
        catch (MissingBlock const&)
        {
          if (!write || this->keys().K() != *this->owner())
            return nullptr;
          auto block = this->make_block<blocks::MutableBlock>(
            elle::serialization::binary::serialize(ServicesTypes()));
          this->seal_and_insert(*block);
          auto beacon = std::make_unique<NB>(
            *this, this->owner(),
            "infinit/services",
            elle::serialization::binary::serialize(block->address()));
          this->insert(std::move(beacon));
          return block;
        }
      }

      bool
      Doughnut::verify(Passport const& passport,
                         bool require_write,
                         bool require_storage,
                         bool require_sign)
      {
        ELLE_TRACE_SCOPE("%s: validating passport %s", this, passport);
        if (  (require_write && !passport.allow_write())
           || (require_storage && !passport.allow_storage())
           || (require_sign && !passport.allow_sign())
         )
        {
          ELLE_TRACE("%s: passport permissions mismatch", *this);
          return false;
        }
        if (!passport.certifier() || *passport.certifier() == *this->owner())
        {
          ELLE_TRACE("%s: validating with owner key", *this);
          return passport.verify(*this->owner());
        }
        if (!passport.verify(*passport.certifier()))
        {
          ELLE_TRACE("%s: validating with certifier key %x", *this,
                     *passport.certifier());
          return false;
        }
        // fetch passport for certifier
        try
        {
          auto const addr = UB::hash_address(*passport.certifier(), *this);
          auto block = this->fetch(addr);
          auto ub = elle::cast<UB>::runtime(block);
          if (!ub->passport())
          {
            ELLE_TRACE("%s: certifier RUB does not contain a passport", *this);
            return false;
          }
          return verify(*ub->passport(), false, false, true);
        }
        catch (elle::Exception const& e)
        {
          ELLE_TRACE("%s: exception fetching/validating: %s",
                     *this, e);
          return false;
        }
      }

      int
      Doughnut::ensure_key(std::shared_ptr<elle::cryptography::rsa::PublicKey> const& k)
      {
        auto it = this->_key_cache.get<0>().find(*k);
        if (it == this->_key_cache.get<0>().end())
        {
          int index = this->_key_cache.get<0>().size();
          this->_key_cache.insert(KeyHash(index, k));
          return index;
        }
        else
          return it->hash;
      }

      std::shared_ptr<elle::cryptography::rsa::PublicKey>
      Doughnut::resolve_key(uint64_t hash)
      {
        ELLE_DUMP("%s: resolve key from %x", this, hash);
        auto it = this->_key_cache.get<1>().find(hash);
        if (it == this->_key_cache.get<1>().end())
          elle::err("%s: failed to resolve key hash locally: %x", this, hash);
        else
        {
          ELLE_DUMP("%s: resolved from cache: %x", this, hash);
          return it->key;
        }
      }

      void
      Doughnut::print(std::ostream& out) const
      {
        elle::fprintf(out, "%s(%f)", elle::type_info(*this), this->id());
      }

      Configuration::~Configuration()
      {}

      Configuration::Configuration(
        Address id_,
        std::unique_ptr<consensus::Configuration> consensus_,
        std::unique_ptr<overlay::Configuration> overlay_,
        std::unique_ptr<silo::StorageConfig> storage,
        elle::cryptography::rsa::KeyPair keys_,
        std::shared_ptr<elle::cryptography::rsa::PublicKey> owner_,
        Passport passport_,
        boost::optional<std::string> name_,
        boost::optional<int> port_,
        elle::Version version,
        AdminKeys admin_keys,
        std::vector<Endpoints> peers,
        boost::optional<std::chrono::milliseconds> tcp_heartbeat_,
        EncryptOptions encrypt_options)
        : ModelConfig(std::move(storage), std::move(version))
        , id(std::move(id_))
        , consensus(std::move(consensus_))
        , overlay(std::move(overlay_))
        , keys(std::move(keys_))
        , owner(std::move(owner_))
        , passport(std::move(passport_))
        , name(std::move(name_))
        , port(std::move(port_))
        , admin_keys(std::move(admin_keys))
        , peers(std::move(peers))
        , tcp_heartbeat(tcp_heartbeat_)
        , encrypt_options(std::move(encrypt_options))
      {}

      Configuration::Configuration(elle::serialization::SerializerIn& s)
        : ModelConfig(s)
        , id(s.deserialize<Address>("id"))
        , consensus(s.deserialize<std::unique_ptr<consensus::Configuration>>(
                      "consensus"))
        , overlay(s.deserialize<std::unique_ptr<overlay::Configuration>>(
                    "overlay"))
        , keys(s.deserialize<elle::cryptography::rsa::KeyPair>("keys"))
        , owner(
          s.deserialize<std::shared_ptr<elle::cryptography::rsa::PublicKey>>("owner"))
        , passport(s.deserialize<Passport>("passport"))
        , name(s.deserialize<boost::optional<std::string>>("name"))
        , port(s.deserialize<boost::optional<int>>("port"))
      {
        try
        {
          s.serialize("admin_keys", this->admin_keys);
        }
        catch (elle::serialization::Error const&)
        {
        }
        try
        {
          s.serialize("peers", this->peers);
        }
        catch (elle::serialization::Error const&)
        {
        }
        s.serialize("tcp-heartbeat", this->tcp_heartbeat);
        try
        {
          s.serialize("encrypt_options", this->encrypt_options);
        }
        catch (elle::serialization::Error const&)
        {
        }
      }

      void
      Configuration::serialize(elle::serialization::Serializer& s)
      {
        ModelConfig::serialize(s);
        s.serialize("id", this->id);
        s.serialize("consensus", this->consensus);
        s.serialize("overlay", this->overlay);
        s.serialize("keys", this->keys);
        s.serialize("owner", this->owner);
        s.serialize("passport", this->passport);
        s.serialize("name", this->name);
        s.serialize("port", this->port);
        s.serialize("admin_keys", this->admin_keys);
        try
        {
          s.serialize("peers", this->peers);
        }
        catch (elle::serialization::Error const&)
        {}
        s.serialize("tcp-heartbeat", this->tcp_heartbeat);
        try
        {
          s.serialize("encrypt_options", this->encrypt_options);
        }
        catch (elle::serialization::Error const&)
        {
        }
      }

      std::unique_ptr<infinit::model::Model>
      Configuration::make(bool client,
                          bfs::path const& dir)
      {
        return this->make(client, dir, false, false);
      }

      std::unique_ptr<Doughnut>
      Configuration::make(
        bool client,
        bfs::path const& p,
        bool async,
        bool cache,
        boost::optional<int> cache_size,
        boost::optional<std::chrono::seconds> cache_ttl,
        boost::optional<std::chrono::seconds> cache_invalidation,
        boost::optional<uint64_t> disk_cache_size,
        boost::optional<elle::Version> version,
        boost::optional<int> port_,
        boost::optional<boost::asio::ip::address> listen_address,
        boost::optional<std::string> rdv_host,
        boost::optional<bool> resign_on_shutdown,
        boost::optional<bfs::path> monitoring_socket_path)
      {
        auto make_consensus =
          [&] (Doughnut& dht)
          {
            if (!this->consensus)
            {
              elle::err(
                "invalid network configuration, missing field \"consensus\"");
            }
            auto consensus = this->consensus->make(dht);
            if (async)
            {
              consensus = std::make_unique<consensus::Async>(
                std::move(consensus), p / "async");
            }
            if (cache)
            {
              consensus = std::make_unique<consensus::Cache>(
                std::move(consensus),
                std::move(cache_size),
                std::move(cache_invalidation),
                std::move(cache_ttl),
                p / "cache",
                std::move(disk_cache_size)
                );
            }
            return consensus;
          };
        auto make_overlay =
          [&] (Doughnut& dht, std::shared_ptr<Local> local)
          {
            if (!this->overlay)
              elle::err(
                "invalid network configuration, missing field \"overlay\"");
            return this->overlay->make(std::move(local), &dht);
          };
        std::unique_ptr<silo::Storage> storage;
        if (this->storage)
          storage = this->storage->make();
        return std::make_unique<infinit::model::doughnut::Doughnut>(
          this->id,
          std::make_shared<elle::cryptography::rsa::KeyPair>(keys),
          owner,
          passport,
          make_consensus,
          make_overlay,
          port_.value_or(this->port.value_or(0)),
          std::move(listen_address),
          std::move(storage),
          client ? this->name : boost::optional<std::string>(),
          version.value_or(this->version),
          admin_keys,
          std::move(rdv_host),
          std::move(monitoring_socket_path),
          this->overlay->rpc_protocol,
          doughnut::tcp_heartbeat = this->tcp_heartbeat,
          doughnut::encrypt_options = this->encrypt_options,
          doughnut::resign_on_shutdown = resign_on_shutdown.value_or(false));
      }

      std::string
      short_key_hash(elle::cryptography::rsa::PublicKey const& pub)
      {
        auto const key_hash = UB::hash(pub);
        auto const hex_hash = elle::format::hexadecimal::encode(key_hash);
        return elle::sprintf("#%s", hex_hash.substr(0, 6));
      }
    }
  }
}
