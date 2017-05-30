#pragma once

#include <elle/algorithm.hh>
#include <elle/factory.hh>

#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kouncil/Kouncil.hh>
#include <infinit/silo/Memory.hh>

namespace dht = infinit::model::doughnut;
namespace blocks = infinit::model::blocks;

using Address = infinit::model::Address;
using Endpoint = infinit::model::Endpoint;
using Endpoints = infinit::model::Endpoints;
using NodeLocation = infinit::model::NodeLocation;

/*----------.
| Overlay.  |
`----------*/

class Overlay
  : public infinit::overlay::Overlay
{
public:
  using Super = infinit::overlay::Overlay;

  Overlay(infinit::model::doughnut::Doughnut* d,
          std::shared_ptr<infinit::model::doughnut::Local> local,
          bool yield = false)
    : Super(d, local)
    , _yield(yield)
  {
    if (local)
    {
      local->on_store().connect(
        [this] (blocks::Block const& block)
        {
          this->_blocks.emplace(block.address());
        });
      local->on_remove().connect(
        [this] (infinit::model::Address addr)
        {
          this->_blocks.erase(addr);
        });
      for (auto const& addr: local->storage()->list())
        this->_blocks.emplace(addr);
    }
    this->_peers.emplace(this);
  }

  ~Overlay()
  {
    this->disconnect_all();
  }

  void
  _cleanup() override
  {
    this->disconnect_all();
  }

  void
  _discover(infinit::model::NodeLocations const& peers) override
  {
    ELLE_ABORT("not implemented");
  }

  bool
  _discovered(infinit::model::Address id) override
  {
    ELLE_ABORT("not implemented");
  }

  static
  std::unique_ptr<Overlay>
  make(infinit::model::doughnut::Doughnut& d,
       std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return std::make_unique<Overlay>(&d, local);
  }

  static
  std::unique_ptr<Overlay>
  make_yield(infinit::model::doughnut::Doughnut& d,
             std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return std::make_unique<Overlay>(&d, local, true);
  }

  void
  connect(Overlay& other)
  {
    if (this->_peers.emplace(&other).second)
      this->on_discovery()(
        infinit::model::NodeLocation(other.doughnut()->id(), {}),
        !other.doughnut()->local());
    if (other._peers.emplace(this).second)
      other.on_discovery()(
        infinit::model::NodeLocation(this->doughnut()->id(), {}),
        !this->doughnut()->local());
  }

  void
  connect_recursive(Overlay& other)
  {
    for (auto overlay: std::unordered_set<Overlay*>(other._peers))
      this->connect(*overlay);
  }

  void
  disconnect(Overlay& other)
  {
    bool me = this->_peers.erase(&other);
    if (other._peers.erase(this))
      other.on_disappearance()(this->doughnut()->id(), !this->doughnut()->local());
    if (me)
      this->on_disappearance()(other.doughnut()->id(), !other.doughnut()->local());
  }

  void
  disconnect_all()
  {
    for (auto peer: std::unordered_set<Overlay*>(this->_peers))
      if (peer != this)
        this->disconnect(*peer);
  }

  std::string
  type_name() const override
  {
    return "test";
  }

  elle::json::Array
  peer_list() const override
  {
    return {};
  }

  elle::json::Object
  stats() const override
  {
    return {{"type", this->type_name()}};
  }

protected:
  MemberGenerator
  _allocate(infinit::model::Address address, int n) const override
  {
    return this->_find(address, n, true);
  }

  MemberGenerator
  _lookup(infinit::model::Address address, int n, bool) const override
  {
    return this->_find(address, n, false);
  }

  MemberGenerator
  _find(infinit::model::Address address, int n, bool write) const
  {
    if (_yield)
      elle::reactor::yield();
    ELLE_LOG_COMPONENT("tests.Overlay");
    ELLE_TRACE_SCOPE("%s: lookup %s%s owners for %f",
                     this, n, write ? " new" : "", address);
    return elle::reactor::generator<Overlay::WeakMember>(
      [=]
      (MemberGenerator::yielder const& yield)
      {
        if (elle::contains(this->_fail_addresses, address))
          return;
        int count = n;
        auto it = this->_partial_addresses.find(address);
        if (it != this->_partial_addresses.end())
          count = it->second;
        if (write)
          for (auto& peer: this->_peers)
          {
            if (count == 0)
              return;
            if (peer->local() && peer->storing())
            {
              yield(peer->local());
              --count;
            }
          }
        else
          for (auto* peer: this->_peers)
          {
            if (count == 0)
              return;
            if (contains(peer->_blocks, address))
            {
              yield(peer->local());
              --count;
            }
          }
        this->_looked_up(address);
      });
  }

  WeakMember
  _lookup_node(infinit::model::Address id) const override
  {
    for (auto* peer: this->_peers)
      if (peer->local() && peer->local()->id() == id)
        return peer->local();
    elle::err("no such node: %f", id);
  }

  ELLE_ATTRIBUTE_R(bool, yield);
  ELLE_ATTRIBUTE_RX(std::unordered_set<Overlay*>, peers);
  ELLE_ATTRIBUTE_R(std::unordered_set<infinit::model::Address>, blocks);
  ELLE_ATTRIBUTE_RX(boost::signals2::signal<void(infinit::model::Address)>,
                    looked_up);
  ELLE_ATTRIBUTE_RX(std::unordered_set<infinit::model::Address>, fail_addresses);
  ELLE_ATTRIBUTE_RX((std::unordered_map<infinit::model::Address, int>), partial_addresses);
};

ELLE_DAS_SYMBOL(paxos);
ELLE_DAS_SYMBOL(keys);
ELLE_DAS_SYMBOL(owner);
ELLE_DAS_SYMBOL(id);
ELLE_DAS_SYMBOL(node_timeout);
ELLE_DAS_SYMBOL(storage);
ELLE_DAS_SYMBOL(make_overlay);
ELLE_DAS_SYMBOL(version);
ELLE_DAS_SYMBOL(with_cache);
ELLE_DAS_SYMBOL(user_name);
ELLE_DAS_SYMBOL(yielding_overlay);
ELLE_DAS_SYMBOL(protocol);
ELLE_DAS_SYMBOL(port);

std::unique_ptr<dht::consensus::Consensus>
add_cache(bool enable, std::unique_ptr<dht::consensus::Consensus> c)
{
  if (enable)
    return std::make_unique<
      infinit::model::doughnut::consensus::Cache>
        (std::move(c), 1000);
  else
    return c;
}


/*------.
| DHT.  |
`------*/

class DHT
  : public elle::Printable
{
  ELLE_LOG_COMPONENT("tests.DHT");
public:
  using make_overlay_t
    = std::function<std::unique_ptr<infinit::overlay::Overlay>
                    (infinit::model::doughnut::Doughnut& d,
                     std::shared_ptr< infinit::model::doughnut::Local> local)>;

  template <typename ... Args>
  DHT(Args&& ... args)
  {
    ELLE_TRACE("contruct: %s", this);
    // FIXME: use named::extend to not repeat dht::Doughnut arguments
    elle::das::named::prototype(
      paxos = true,
      keys = elle::cryptography::rsa::keypair::generate(512),
      owner = boost::optional<elle::cryptography::rsa::KeyPair>(),
      id = infinit::model::Address::random(0), // FIXME
      storage = elle::factory(
        [] { return std::make_unique<infinit::silo::Memory>(); }),
      version = boost::optional<elle::Version>(),
      make_overlay = &Overlay::make,
      dht::consensus_builder = dht::Doughnut::ConsensusBuilder(),
      dht::consensus::rebalance_auto_expand = true,
      dht::consensus::node_timeout = std::chrono::minutes(10),
      with_cache = false,
      user_name = "",
      yielding_overlay = false,
      protocol = dht::Protocol::tcp,
      port = boost::none,
      dht::connect_timeout =
        elle::defaulted(std::chrono::milliseconds(5000)),
      dht::soft_fail_timeout =
        elle::defaulted(std::chrono::milliseconds(20000)),
      dht::soft_fail_running = elle::defaulted(false),
      dht::resign_on_shutdown = false
      ).call([this] (bool paxos,
                     elle::cryptography::rsa::KeyPair keys,
                     boost::optional<elle::cryptography::rsa::KeyPair> owner,
                     infinit::model::Address id,
                     std::unique_ptr<infinit::silo::Silo> storage,
                     boost::optional<elle::Version> version,
                     make_overlay_t make_overlay,
                     dht::Doughnut::ConsensusBuilder consensus_builder,
                     bool rebalance_auto_expand,
                     std::chrono::system_clock::duration node_timeout,
                     bool with_cache,
                     std::string const& user_name,
                     bool yielding_overlay,
                     dht::Protocol p,
                     boost::optional<int> port,
                     elle::Defaulted<std::chrono::milliseconds> connect_timeout,
                     elle::Defaulted<std::chrono::milliseconds> soft_fail_timeout,
                     elle::Defaulted<bool> soft_fail_running,
                     bool resign_on_shutdown)
             {
               this->init(paxos,
                          keys,
                          owner ? *owner : keys,
                          id,
                          std::move(storage),
                          version,
                          std::move(consensus_builder),
                          std::move(make_overlay),
                          rebalance_auto_expand,
                          node_timeout,
                          with_cache,
                          user_name,
                          yielding_overlay,
                          p,
                          port,
                          connect_timeout,
                          soft_fail_timeout,
                          soft_fail_running,
                          resign_on_shutdown);
              }, std::forward<Args>(args)...);
  }

  elle::reactor::network::TCPSocket
  connect_tcp()
  {
    for (auto const& ep: this->dht->local()->server_endpoints())
      try
      {
        ELLE_TRACE("connect_tcp: %s, endpoint: %s", this, ep);
        return ep.tcp();
      }
      catch (...)
      {
        ELLE_LOG("%s: connection failed: %s",
                 *this, elle::exception_string());
      }
    ELLE_ERR("connect_tcp: all connection attempts failed");
    abort();
  }

  std::shared_ptr<dht::Doughnut> dht;
  Overlay* overlay;

protected:
  void
  print(std::ostream& s) const override
  {
    elle::fprintf(s, "%s", this->dht);
  }

private:
  void
  init(bool paxos,
       elle::cryptography::rsa::KeyPair keys_,
       elle::cryptography::rsa::KeyPair owner,
       infinit::model::Address id,
       std::unique_ptr<infinit::silo::Silo> storage,
       boost::optional<elle::Version> version,
       dht::Doughnut::ConsensusBuilder consensus_builder,
       make_overlay_t make_overlay,
       bool rebalance_auto_expand,
       std::chrono::system_clock::duration node_timeout,
       bool with_cache,
       std::string const& user_name,
       bool yielding_overlay,
       dht::Protocol p,
       boost::optional<int> port,
       elle::Defaulted<std::chrono::milliseconds> connect_timeout,
       elle::Defaulted<std::chrono::milliseconds> soft_fail_timeout,
       elle::Defaulted<bool> soft_fail_running,
       bool resign_on_shutdown)
  {
    auto keys =
      std::make_shared<elle::cryptography::rsa::KeyPair>(std::move(keys_));
    auto consensus = [&]() -> dht::Doughnut::ConsensusBuilder
      {
        if (consensus_builder)
          return [&] (dht::Doughnut& dht)
          {
            return add_cache(with_cache, consensus_builder(dht));
          };
        else if (paxos)
          return
            [&] (dht::Doughnut& dht)
            {
              return add_cache(
                with_cache,
                std::make_unique<dht::consensus::Paxos>(
                  dht::consensus::doughnut = dht,
                  dht::consensus::replication_factor = 3,
                  dht::consensus::rebalance_auto_expand = rebalance_auto_expand,
                  dht::consensus::node_timeout = node_timeout));
            };
        else
          return
            [&] (dht::Doughnut& dht)
            {
              return add_cache(
                with_cache, std::make_unique<dht::consensus::Consensus>(dht));
            };
      }();
    auto passport = dht::Passport(keys->K(), "network-name", owner);
    auto overlay_builder =
          [this, &make_overlay] (
        infinit::model::doughnut::Doughnut& d,
        std::shared_ptr<infinit::model::doughnut::Local> local)
      {
        auto res = make_overlay(d, std::move(local));
        this->overlay = dynamic_cast<Overlay*>(res.get());
        return res;
      };
    if (user_name.empty())
      this->dht = std::make_shared<dht::Doughnut>(
        dht::id = id,
        dht::keys = keys,
        dht::owner = owner.public_key(),
        dht::passport = passport,
        dht::consensus_builder = consensus,
        dht::overlay_builder = overlay_builder,
        dht::port = port,
        dht::storage = std::move(storage),
        dht::version = version,
        dht::protocol = p,
        dht::connect_timeout = connect_timeout,
        dht::soft_fail_timeout = soft_fail_timeout,
        dht::soft_fail_running = soft_fail_running,
        dht::resign_on_shutdown = resign_on_shutdown);
    else
      this->dht = std::make_shared<dht::Doughnut>(
        dht::name = user_name,
        dht::id = id,
        dht::keys = keys,
        dht::owner = owner.public_key(),
        dht::passport = passport,
        dht::consensus_builder = consensus,
        dht::overlay_builder = overlay_builder,
        dht::port = port,
        dht::storage = std::move(storage),
        dht::version = version,
        dht::protocol = p,
        dht::connect_timeout = connect_timeout,
        dht::soft_fail_timeout = soft_fail_timeout,
        dht::soft_fail_running = soft_fail_running,
        dht::resign_on_shutdown = resign_on_shutdown);
  }
};

class NoCheatConsensus
  : public infinit::model::doughnut::consensus::Consensus
{
public:
  using Super = infinit::model::doughnut::consensus::Consensus;
  NoCheatConsensus(std::unique_ptr<Super> backend)
    : Super(backend->doughnut())
    , _backend(std::move(backend))
  {}

protected:
  std::unique_ptr<infinit::model::doughnut::Local>
  make_local(boost::optional<int> port,
             boost::optional<boost::asio::ip::address> listen,
             std::unique_ptr<infinit::silo::Silo> storage) override
  {
    return _backend->make_local(port, listen, std::move(storage));
  }

  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(infinit::model::Address address, boost::optional<int> local_version) override
  {
    auto res = _backend->fetch(address, local_version);
    if (!res)
      return res;
    elle::Buffer buf;
    {
      elle::IOStream os(buf.ostreambuf());
      elle::serialization::binary::serialize(res, os);
    }
    elle::IOStream is(buf.istreambuf());
    elle::serialization::Context ctx;
    ctx.set(&doughnut());
    res = elle::serialization::binary::deserialize<std::unique_ptr<blocks::Block>>(
      is, true, ctx);
    return res;
  }

  void
  _store(std::unique_ptr<infinit::model::blocks::Block> block,
    infinit::model::StoreMode mode,
    std::unique_ptr<infinit::model::ConflictResolver> resolver) override
  {
    this->_backend->store(std::move(block), mode, std::move(resolver));
  }

  void
  _remove(infinit::model::Address address, infinit::model::blocks::RemoveSignature rs) override
  {
    if (rs.block)
    {
      elle::Buffer buf;
      {
        elle::IOStream os(buf.ostreambuf());
        elle::serialization::binary::serialize(rs.block, os);
      }
      elle::IOStream is(buf.istreambuf());
      elle::serialization::Context ctx;
      ctx.set(&doughnut());
      auto res = elle::serialization::binary::deserialize<std::unique_ptr<blocks::Block>>(
        is, true, ctx);
      rs.block = std::move(res);
    }
    _backend->remove(address, rs);
  }

  std::unique_ptr<Super> _backend;
};

dht::Doughnut::ConsensusBuilder
no_cheat_consensus(bool paxos = true)
{
  return [paxos] (dht::Doughnut& dht)
  {
    std::unique_ptr<dht::consensus::Consensus> c;
    if (paxos)
      c = std::make_unique<dht::consensus::Paxos>(
        dht::consensus::doughnut = dht,
        dht::consensus::replication_factor = 3);
    else
      c = std::make_unique<dht::consensus::Consensus>(dht);
    return std::make_unique<NoCheatConsensus>(std::move(c));
  };
}

/// The Kelips node in this client.
auto*
get_kelips(DHT& client)
{
  return dynamic_cast<infinit::overlay::kelips::Node*>
    (client.dht->overlay().get());
}

/// The Kouncil node in this client.
auto*
get_kouncil(DHT& client)
{
  return dynamic_cast<infinit::overlay::kouncil::Kouncil*>
    (client.dht->overlay().get());
}


/// An Address easy to read in the logs.
infinit::model::Address
special_id(int i)
{
  assert(i);
  infinit::model::Address::Value id;
  memset(&id, 0, sizeof(id));
  id[0] = i;
  return id;
}


/// Watch and warn about @a target being evicted by @a dht.
boost::signals2::scoped_connection
monitor_eviction(DHT& dht, DHT& target)
{
  ELLE_LOG_COMPONENT("infinit.tests.DHT");
  if (auto kouncil = get_kouncil(dht))
    return kouncil->on_eviction().connect([&](Address id)
    {
      if (id == target.dht->id())
        ELLE_ERR
          ("dht = %s was waiting for target = %s, but target was evicted by dht",
           dht, target);
    });
  else
    return {};
}

/// Let an overlay peer discover another one.
///
/// @param dht     the discoverer
/// @param target  the discovered
/// @param loc     the adress via which @a target is to be discovered
/// @param wait    whether to wait for @a dht to discover @a target
/// @param wait_back  whether to wait for @a target to discover @a dht
void
discover(DHT& dht,
         DHT& target,
         infinit::model::NodeLocation const& loc,
         bool wait = false,
         bool wait_back = false)
{
  ELLE_LOG_COMPONENT("infinit.tests.DHT");
  auto discovered = elle::reactor::waiter(
    dht.dht->overlay()->on_discovery(),
    [&] (NodeLocation const& l, bool) { return l.id() == target.dht->id(); });
  auto discovered_back = elle::reactor::waiter(
    target.dht->overlay()->on_discovery(),
    [&] (NodeLocation const& l, bool) { return l.id() == dht.dht->id(); });
  auto eviction = monitor_eviction(dht, target);
  auto eviction_back = monitor_eviction(target, dht);
  ELLE_LOG("%f invited to discover %f via %f", dht, target, loc)
    dht.dht->overlay()->discover(loc);
  if (wait)
  {
    elle::reactor::wait(discovered);
    ELLE_LOG("%s discovered %s", dht, target);
  }
  if (wait_back)
  {
    elle::reactor::wait(discovered_back);
    ELLE_LOG("%s discovered back by %s", dht, target);
  }
}

/// Let an overlay peer discover another one.
///
/// @param dht     the discoverer
/// @param target  the discovered
/// @param wait    whether to wait for @a dht to discover @a target
/// @param wait_back  whether to wait for @a target to discover @a dht
void
discover(DHT& dht,
         DHT& target,
         bool anonymous,
         bool onlyfirst = false,
         bool wait = false,
         bool wait_back = false)
{
  assert(target.dht);
  assert(target.dht->local());
  auto const all_eps = target.dht->local()->server_endpoints();
  assert(!all_eps.empty());
  auto const eps = onlyfirst ? Endpoints{*all_eps.begin()} : all_eps;
  auto const addr = anonymous ? Address::null : target.dht->id();
  auto const loc = NodeLocation{addr, eps};
  discover(dht, target, loc, wait, wait_back);
}
