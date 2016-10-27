#ifndef DHT_HH
# define DHT_HH

# include <elle/named.hh>

# include <infinit/model/doughnut/Doughnut.hh>
# include <infinit/model/doughnut/Local.hh>
# include <infinit/model/doughnut/Cache.hh>
# include <infinit/model/doughnut/consensus/Paxos.hh>
# include <infinit/storage/Memory.hh>

namespace dht = infinit::model::doughnut;
namespace blocks = infinit::model::blocks;

class Overlay
  : public infinit::overlay::Overlay
{
public:
  typedef infinit::overlay::Overlay Super;

  Overlay(infinit::model::doughnut::Doughnut* d,
          std::shared_ptr< infinit::model::doughnut::Local> local,
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
  _discover(infinit::model::NodeLocations const& peers) override
  {
    ELLE_ABORT("not implemented");
  }

  static
  std::unique_ptr<Overlay>
  make(infinit::model::doughnut::Doughnut& d,
       std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return elle::make_unique<Overlay>(&d, local);
  }

  static
  std::unique_ptr<Overlay>
  make_yield(infinit::model::doughnut::Doughnut& d,
       std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return elle::make_unique<Overlay>(&d, local, true);
  }

  void
  connect(Overlay& other)
  {
    if (this->_peers.emplace(&other).second)
      this->on_discover()(
        infinit::model::NodeLocation(other.doughnut()->id(), {}),
        !other.doughnut()->local());
    if (other._peers.emplace(this).second)
      other.on_discover()(
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
      other.on_disappear()(this->doughnut()->id(), !this->doughnut()->local());
    if (me)
      this->on_disappear()(other.doughnut()->id(), !other.doughnut()->local());
  }

  void
  disconnect_all()
  {
    for (auto peer: std::unordered_set<Overlay*>(this->_peers))
      if (peer != this)
        this->disconnect(*peer);
  }

protected:
  virtual
  reactor::Generator<WeakMember>
  _lookup(infinit::model::Address address,
          int n,
          infinit::overlay::Operation op) const override
  {
    if (_yield)
      reactor::yield();
    ELLE_LOG_COMPONENT("Overlay");
    bool write = op == infinit::overlay::OP_INSERT;
    ELLE_TRACE_SCOPE("%s: lookup %s%s owners for %f",
                     this, n, write ? " new" : "", address);
    return reactor::generator<Overlay::WeakMember>(
      [=]
      (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
      {
        if (this->_fail_addresses.find(address) != this->_fail_addresses.end())
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
            if (peer->local())
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

  virtual
  WeakMember
  _lookup_node(infinit::model::Address id) const override
  {
    for (auto* peer: this->_peers)
      if (peer->local() && peer->local()->id() == id)
        return peer->local();
    throw elle::Error(elle::sprintf("no such node: %f", id));
  }

  ELLE_ATTRIBUTE_R(bool, yield);
  ELLE_ATTRIBUTE_RX(std::unordered_set<Overlay*>, peers);
  ELLE_ATTRIBUTE_R(std::unordered_set<infinit::model::Address>, blocks);
  ELLE_ATTRIBUTE_RX(boost::signals2::signal<void(infinit::model::Address)>,
                    looked_up);
  ELLE_ATTRIBUTE_RX(std::unordered_set<infinit::model::Address>, fail_addresses);
  ELLE_ATTRIBUTE_RX((std::unordered_map<infinit::model::Address, int>), partial_addresses);
};

NAMED_ARGUMENT(paxos);
NAMED_ARGUMENT(keys);
NAMED_ARGUMENT(owner);
NAMED_ARGUMENT(id);
NAMED_ARGUMENT(node_timeout);
NAMED_ARGUMENT(storage);
NAMED_ARGUMENT(make_overlay);
NAMED_ARGUMENT(make_consensus);
NAMED_ARGUMENT(version);
NAMED_ARGUMENT(with_cache);
NAMED_ARGUMENT(user_name);
NAMED_ARGUMENT(yielding_overlay);
NAMED_ARGUMENT(protocol);
NAMED_ARGUMENT(port);
NAMED_ARGUMENT(monitoring_socket_path_a);

std::unique_ptr<dht::consensus::Consensus>
add_cache(bool enable, std::unique_ptr<dht::consensus::Consensus> c)
{
  if (enable)
    return elle::make_unique<
      infinit::model::doughnut::consensus::Cache>
        (std::move(c), 1000);
  else
    return c;
}

class DHT
{
public:
  template <typename ... Args>
  DHT(Args&& ... args)
  {
    namespace ph = std::placeholders;
    elle::named::prototype(
      paxos = true,
      keys = infinit::cryptography::rsa::keypair::generate(512),
      owner = boost::optional<infinit::cryptography::rsa::KeyPair>(),
      id = infinit::model::Address::random(0), // FIXME
      storage = elle::make_unique<infinit::storage::Memory>(),
      version = boost::optional<elle::Version>(),
      make_overlay = &Overlay::make,
      make_consensus = [] (std::unique_ptr<dht::consensus::Consensus> c)
        -> std::unique_ptr<dht::consensus::Consensus>
      {
        return c;
      },
      dht::consensus::rebalance_auto_expand = true,
      dht::consensus::node_timeout = std::chrono::minutes(10),
      with_cache = false,
      user_name = "",
      yielding_overlay = false,
      protocol = dht::Protocol::all,
      port = boost::none
      ).call([this] (bool paxos,
                     infinit::cryptography::rsa::KeyPair keys,
                     boost::optional<infinit::cryptography::rsa::KeyPair> owner,
                     infinit::model::Address id,
                     std::unique_ptr<infinit::storage::Storage> storage,
                     boost::optional<elle::Version> version,
                     std::function<
                     std::unique_ptr<infinit::overlay::Overlay>(
                       infinit::model::doughnut::Doughnut& d,
                       std::shared_ptr< infinit::model::doughnut::Local> local)>
                       make_overlay,
                     std::function<
                     std::unique_ptr<dht::consensus::Consensus>(
                       std::unique_ptr<dht::consensus::Consensus>
                       )> make_consensus,
                     bool rebalance_auto_expand,
                     std::chrono::system_clock::duration node_timeout,
                     bool with_cache,
                     std::string const& user_name,
                     bool yielding_overlay,
                     dht::Protocol p,
                     boost::optional<int> port)
             {
               this-> init(paxos,
                           keys,
                           owner ? *owner : keys,
                           id,
                           std::move(storage),
                           version,
                           std::move(make_consensus),
                           std::move(make_overlay),
                           rebalance_auto_expand,
                           node_timeout,
                           with_cache,
                           user_name,
                           yielding_overlay,
                           p,
                           port);
              }, std::forward<Args>(args)...);
  }

  std::shared_ptr<dht::Doughnut> dht;
  Overlay* overlay;

private:
  void
  init(bool paxos,
       infinit::cryptography::rsa::KeyPair keys_,
       infinit::cryptography::rsa::KeyPair owner,
       infinit::model::Address id,
       std::unique_ptr<infinit::storage::Storage> storage,
       boost::optional<elle::Version> version,
       std::function<
         std::unique_ptr<dht::consensus::Consensus>(
           std::unique_ptr<dht::consensus::Consensus>)> make_consensus,
       std::function<std::unique_ptr<infinit::overlay::Overlay>(
         infinit::model::doughnut::Doughnut& d,
         std::shared_ptr<infinit::model::doughnut::Local> local)>
         make_overlay,
       bool rebalance_auto_expand,
       std::chrono::system_clock::duration node_timeout,
       bool with_cache,
       std::string const& user_name,
       bool yielding_overlay,
       dht::Protocol p,
       boost::optional<int> port)
  {
    auto keys =
      std::make_shared<infinit::cryptography::rsa::KeyPair>(std::move(keys_));
    dht::Doughnut::ConsensusBuilder consensus;
    if (paxos)
      consensus =
        [&] (dht::Doughnut& dht)
        {
          return add_cache(with_cache, make_consensus(
            elle::make_unique<dht::consensus::Paxos>(
              dht::consensus::doughnut = dht,
              dht::consensus::replication_factor = 3,
              dht::consensus::rebalance_auto_expand = rebalance_auto_expand,
              dht::consensus::node_timeout = node_timeout)));
        };
    else
      consensus =
        [&] (dht::Doughnut& dht)
        {
          return add_cache(with_cache, make_consensus(
            elle::make_unique<dht::consensus::Consensus>(dht)));
        };
    dht::Passport passport(keys->K(), "network-name", owner);
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
      this->dht.reset(new dht::Doughnut(
        dht::id = id,
        dht::keys = keys,
        dht::owner = owner.public_key(),
        dht::passport = passport,
        dht::consensus_builder = consensus,
        dht::overlay_builder = infinit::model::doughnut::Doughnut::OverlayBuilder(overlay_builder),
        dht::port = port,
        dht::storage = std::move(storage),
        dht::version = version,
        dht::protocol = p));
    else
      this->dht = std::make_shared<dht::Doughnut>(
        dht::id = id,
        dht::keys = keys,
        dht::owner = owner.public_key(),
        dht::passport = passport,
        dht::consensus_builder = consensus,
        dht::overlay_builder = infinit::model::doughnut::Doughnut::OverlayBuilder(overlay_builder),
        dht::port = port,
        dht::storage = std::move(storage),
        dht::name = user_name,
        dht::version = version,
        dht::protocol = p);
  }
};

#endif
