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
      local->on_store.connect(
        [this] (blocks::Block const& block,
                infinit::model::StoreMode)
        {
          this->_blocks.emplace(block.address());
        });
    this->_peers.emplace_back(this);
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
        break;
      }
  }

protected:
  virtual
  reactor::Generator<Member>
  _lookup(infinit::model::Address address,
          int n,
          infinit::overlay::Operation op) const override
  {
    ELLE_LOG_COMPONENT("Overlay");
    bool write = op == infinit::overlay::OP_INSERT ||
      op == infinit::overlay::OP_INSERT_OR_UPDATE;
    ELLE_TRACE_SCOPE("%s: lookup %s%s owners for %f",
                     *this, n, write ? " new" : "", address);
    return reactor::generator<Overlay::Member>(
      [=]
      (reactor::Generator<Overlay::Member>::yielder const& yield)
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
      });
  }

  virtual
  Member
  _lookup_node(infinit::model::Address id) override
  {
    for (auto* peer: this->_peers)
      if (peer->local() && peer->local()->id() == id)
        return peer->local();
    throw elle::Error(elle::sprintf("no such node: %s", id));
  }

  ELLE_ATTRIBUTE_RX(std::vector<Overlay*>, peers);
  ELLE_ATTRIBUTE(std::unordered_set<infinit::model::Address>, blocks);
};

NAMED_ARGUMENT(paxos);
NAMED_ARGUMENT(keys);
NAMED_ARGUMENT(keys_a);
NAMED_ARGUMENT(keys_b);
NAMED_ARGUMENT(keys_c);
NAMED_ARGUMENT(id);
NAMED_ARGUMENT(id_a);
NAMED_ARGUMENT(id_b);
NAMED_ARGUMENT(id_c);
NAMED_ARGUMENT(storage);
NAMED_ARGUMENT(storage_a);
NAMED_ARGUMENT(storage_b);
NAMED_ARGUMENT(storage_c);
NAMED_ARGUMENT(make_overlay);
NAMED_ARGUMENT(make_consensus);
NAMED_ARGUMENT(version);
NAMED_ARGUMENT(version_a);
NAMED_ARGUMENT(version_b);
NAMED_ARGUMENT(version_c);

class DHT
{
public:
  template <typename ... Args>
  DHT(Args&& ... args)
  {
    namespace ph = std::placeholders;
    elle::named::prototype(
      paxos = true,
      ::keys = infinit::cryptography::rsa::keypair::generate(512),
      id = infinit::model::Address::random(0), // FIXME
      storage = elle::make_unique<infinit::storage::Memory>(),
      version = boost::optional<elle::Version>(),
      make_overlay = &Overlay::make,
      make_consensus =
      [] (std::unique_ptr<dht::consensus::Consensus> c)
        -> std::unique_ptr<dht::consensus::Consensus>
      {
        return c;
      }
      ).call([this] (bool paxos,
                     infinit::cryptography::rsa::KeyPair keys,
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
                       )> make_consensus)
             {
               this-> init(paxos,
                            std::move(keys),
                            id,
                            std::move(storage),
                            version,
                            std::move(make_consensus));
              }, std::forward<Args>(args)...);
  }

  std::shared_ptr<infinit::cryptography::rsa::KeyPair> keys;
  std::shared_ptr<dht::Doughnut> dht;
  Overlay* overlay;

private:
  void
  init(bool paxos,
       infinit::cryptography::rsa::KeyPair keys,
       infinit::model::Address id,
       std::unique_ptr<infinit::storage::Storage> storage,
       boost::optional<elle::Version> version,
       std::function<
         std::unique_ptr<dht::consensus::Consensus>(
           std::unique_ptr<dht::consensus::Consensus>)> make_consensus)
  {
    this->keys =
      std::make_shared<infinit::cryptography::rsa::KeyPair>(std::move(keys));
    dht::Doughnut::ConsensusBuilder consensus;
    if (paxos)
      consensus =
        [&] (dht::Doughnut& dht)
        {
          return make_consensus(
            elle::make_unique<dht::consensus::Paxos>(dht, 3));
        };
    else
      consensus =
        [&] (dht::Doughnut& dht)
        {
          return make_consensus(
            elle::make_unique<dht::consensus::Consensus>(dht));
        };
    dht::Passport passport(
      this->keys->K(), "network-name", *this->keys);
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
      this->keys,
      this->keys->public_key(),
      passport,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(make_overlay),
      boost::optional<int>(),
      std::move(storage),
      version);
  }
};

#endif
