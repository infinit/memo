#ifndef DHT_HH
# define DHT_HH

# include <elle/named.hh>

# include <infinit/model/doughnut/Doughnut.hh>
# include <infinit/model/doughnut/Local.hh>
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
          infinit::model::Address id)
    : Super(d, local, id)
  {
    if (local)
    {
      local->on_store().connect(
        [this] (blocks::Block const& block)
        {
          this->_blocks.emplace(block.address());
        });
      for (auto const& addr: local->storage()->list())
        this->_blocks.emplace(addr);
    }
    this->_peers.emplace_back(this);
  }

  ~Overlay()
  {
    while (!this->_peers.empty())
      this->disconnect(**this->_peers.begin());
  }

  static
  std::unique_ptr<Overlay>
  make(infinit::model::doughnut::Doughnut& d,
       infinit::model::Address id,
       std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return elle::make_unique<Overlay>(&d, local, id);
  }

  void
  connect(Overlay& other)
  {
    this->_peers.emplace_back(&other);
    other._peers.emplace_back(this);
    this->on_discover()(other.node_id(), !other.doughnut()->local());
    other.on_discover()(this->node_id(), !this->doughnut()->local());
  }

  void
  disconnect(Overlay& other)
  {
    for (auto it = this->_peers.begin(); it != this->_peers.end(); ++it)
      if (*it == &other)
      {
        this->_peers.erase(it);
        break;
      }
    for (auto it = other._peers.begin(); it != other._peers.end(); ++it)
      if (*it == this)
      {
        other._peers.erase(it);
        other.on_disappear()(this->node_id());
        break;
      }
    this->on_disappear()(other.node_id());
  }

protected:
  virtual
  reactor::Generator<WeakMember>
  _lookup(infinit::model::Address address,
          int n,
          infinit::overlay::Operation op) const override
  {
    ELLE_LOG_COMPONENT("Overlay");
    bool write = op == infinit::overlay::OP_INSERT;
    ELLE_TRACE_SCOPE("%s: lookup %s%s owners for %f",
                     this, n, write ? " new" : "", address);
    return reactor::generator<Overlay::WeakMember>(
      [=]
      (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
      {
        int count = n;
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
  _lookup_node(infinit::model::Address id) override
  {
    for (auto* peer: this->_peers)
      if (peer->local() && peer->local()->id() == id)
        return peer->local();
    throw elle::Error(elle::sprintf("no such node: %f", id));
  }

  ELLE_ATTRIBUTE_RX(std::vector<Overlay*>, peers);
  ELLE_ATTRIBUTE(std::unordered_set<infinit::model::Address>, blocks);
  ELLE_ATTRIBUTE_RX(boost::signals2::signal<void(infinit::model::Address)>,
                    looked_up);
};

NAMED_ARGUMENT(paxos);
NAMED_ARGUMENT(keys);
NAMED_ARGUMENT(owner);
NAMED_ARGUMENT(id);
NAMED_ARGUMENT(storage);
NAMED_ARGUMENT(make_overlay);
NAMED_ARGUMENT(make_consensus);
NAMED_ARGUMENT(version);

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
      dht::consensus::rebalance_auto_expand = true
      ).call([this] (bool paxos,
                     infinit::cryptography::rsa::KeyPair keys,
                     boost::optional<infinit::cryptography::rsa::KeyPair> owner,
                     infinit::model::Address id,
                     std::unique_ptr<infinit::storage::Storage> storage,
                     boost::optional<elle::Version> version,
                     std::function<
                     std::unique_ptr<infinit::overlay::Overlay>(
                       infinit::model::doughnut::Doughnut& d,
                       infinit::model::Address id,
                       std::shared_ptr< infinit::model::doughnut::Local> local)>
                       make_overlay,
                     std::function<
                     std::unique_ptr<dht::consensus::Consensus>(
                       std::unique_ptr<dht::consensus::Consensus>
                       )> make_consensus,
                     bool rebalance_auto_expand)
             {
               this-> init(paxos,
                           keys,
                           owner ? *owner : keys,
                           id,
                           std::move(storage),
                           version,
                           std::move(make_consensus),
                           rebalance_auto_expand);
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
       bool rebalance_auto_expand)
  {
    auto keys =
      std::make_shared<infinit::cryptography::rsa::KeyPair>(std::move(keys_));
    dht::Doughnut::ConsensusBuilder consensus;
    if (paxos)
      consensus =
        [&] (dht::Doughnut& dht)
        {
          return make_consensus(
            elle::make_unique<dht::consensus::Paxos>(
              dht::consensus::doughnut = dht,
              dht::consensus::replication_factor = 3,
              dht::consensus::rebalance_auto_expand = rebalance_auto_expand));
        };
    else
      consensus =
        [&] (dht::Doughnut& dht)
        {
          return make_consensus(
            elle::make_unique<dht::consensus::Consensus>(dht));
        };
    dht::Passport passport(keys->K(), "network-name", owner);
    auto make_overlay =
      [this] (
        infinit::model::doughnut::Doughnut& d,
        infinit::model::Address id,
        std::shared_ptr<infinit::model::doughnut::Local> local)
      {
        auto res = Overlay::make(d, std::move(id), std::move(local));
        this->overlay = res.get();
        return res;
      };
    this->dht = std::make_shared<dht::Doughnut>(
      id,
      keys,
      owner.public_key(),
      passport,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(make_overlay),
      boost::optional<int>(),
      std::move(storage),
      version);
  }
};

#endif
