#include <memory>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/named.hh>
#include <elle/test.hh>
#include <elle/utils.hh>
#include <elle/Version.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/version.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.test");

namespace dht = infinit::model::doughnut;
namespace storage = infinit::storage;

NAMED_ARGUMENT(paxos);
NAMED_ARGUMENT(keys_a);
NAMED_ARGUMENT(keys_b);
NAMED_ARGUMENT(keys_c);
NAMED_ARGUMENT(id_a);
NAMED_ARGUMENT(id_b);
NAMED_ARGUMENT(id_c);
NAMED_ARGUMENT(storage_a);
NAMED_ARGUMENT(storage_b);
NAMED_ARGUMENT(storage_c);
NAMED_ARGUMENT(make_overlay);
NAMED_ARGUMENT(make_consensus);
NAMED_ARGUMENT(version_a);
NAMED_ARGUMENT(version_b);
NAMED_ARGUMENT(version_c);

class DHTs
{
public:
  template <typename ... Args>
  DHTs(Args&& ... args)
  {
    namespace ph = std::placeholders;
    elle::named::prototype(
      paxos = true,
      ::keys_a = infinit::cryptography::rsa::keypair::generate(512),
      ::keys_b = infinit::cryptography::rsa::keypair::generate(512),
      ::keys_c = infinit::cryptography::rsa::keypair::generate(512),
      id_a = infinit::model::Address::random(),
      id_b = infinit::model::Address::random(),
      id_c = infinit::model::Address::random(),
      storage_a = nullptr,
      storage_b = nullptr,
      storage_c = nullptr,
      version_a = boost::optional<elle::Version>(),
      version_b = boost::optional<elle::Version>(),
      version_c = boost::optional<elle::Version>(),
      make_overlay =
      [] (int,
          infinit::model::Address id,
          infinit::overlay::Stonehenge::Peers peers,
          std::shared_ptr<infinit::model::doughnut::Local> local,
          infinit::model::doughnut::Doughnut& d)
      {
        return elle::make_unique<infinit::overlay::Stonehenge>(
          id, peers, std::move(local), &d);
      },
      make_consensus =
      [] (std::unique_ptr<dht::consensus::Consensus> c)
        -> std::unique_ptr<dht::consensus::Consensus>
      {
        return c;
      }
      ).call([this] (bool paxos,
                      infinit::cryptography::rsa::KeyPair keys_a,
                      infinit::cryptography::rsa::KeyPair keys_b,
                      infinit::cryptography::rsa::KeyPair keys_c,
                      infinit::model::Address id_a,
                      infinit::model::Address id_b,
                      infinit::model::Address id_c,
                      std::unique_ptr<storage::Storage> storage_a,
                      std::unique_ptr<storage::Storage> storage_b,
                      std::unique_ptr<storage::Storage> storage_c,
                      boost::optional<elle::Version> version_a,
                      boost::optional<elle::Version> version_b,
                      boost::optional<elle::Version> version_c,
                      std::function<
                        std::unique_ptr<infinit::overlay::Stonehenge>(
                          int,
                          infinit::model::Address id,
                          infinit::overlay::Stonehenge::Peers peers,
                          std::shared_ptr<
                            infinit::model::doughnut::Local> local,
                          infinit::model::doughnut::Doughnut& d)> make_overlay,
                      std::function<
                        std::unique_ptr<dht::consensus::Consensus>(
                          std::unique_ptr<dht::consensus::Consensus>
                          )> make_consensus)
              {
                this-> init(paxos,
                            std::move(keys_a),
                            std::move(keys_b),
                            std::move(keys_c),
                            id_a, id_b, id_c,
                            std::move(storage_a),
                            std::move(storage_b),
                            std::move(storage_c) ,
                            version_a,
                            version_b,
                            version_c,
                            std::move(make_overlay),
                            std::move(make_consensus));
              }, std::forward<Args>(args)...);
  }

  std::shared_ptr<infinit::cryptography::rsa::KeyPair> keys_a;
  std::shared_ptr<infinit::cryptography::rsa::KeyPair> keys_b;
  std::shared_ptr<infinit::cryptography::rsa::KeyPair> keys_c;
  std::shared_ptr<dht::Doughnut> dht_a;
  std::shared_ptr<dht::Doughnut> dht_b;
  std::shared_ptr<dht::Doughnut> dht_c;

private:
  void
  init(bool paxos,
       infinit::cryptography::rsa::KeyPair keys_a,
       infinit::cryptography::rsa::KeyPair keys_b,
       infinit::cryptography::rsa::KeyPair keys_c,
       infinit::model::Address id_a,
       infinit::model::Address id_b,
       infinit::model::Address id_c,
       std::unique_ptr<storage::Storage> storage_a,
       std::unique_ptr<storage::Storage> storage_b,
       std::unique_ptr<storage::Storage> storage_c,
       boost::optional<elle::Version> version_a,
       boost::optional<elle::Version> version_b,
       boost::optional<elle::Version> version_c,
       std::function<
         std::unique_ptr<infinit::overlay::Stonehenge>(
           int,
           infinit::model::Address id,
           infinit::overlay::Stonehenge::Peers peers,
           std::shared_ptr<infinit::model::doughnut::Local> local,
           infinit::model::doughnut::Doughnut& d)> make_overlay,
       std::function<
         std::unique_ptr<dht::consensus::Consensus>(
           std::unique_ptr<dht::consensus::Consensus>)> make_consensus)
  {
    this->keys_a =
      std::make_shared<infinit::cryptography::rsa::KeyPair>(std::move(keys_a));
    this->keys_b =
      std::make_shared<infinit::cryptography::rsa::KeyPair>(std::move(keys_b));
    this->keys_c =
      std::make_shared<infinit::cryptography::rsa::KeyPair>(std::move(keys_c));
    if (!storage_a)
      storage_a = elle::make_unique<storage::Memory>();
    if (!storage_b)
      storage_b = elle::make_unique<storage::Memory>();
    if (!storage_c)
      storage_c = elle::make_unique<storage::Memory>();
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
    dht::Passport passport_a(
      this->keys_a->K(), "network-name", this->keys_a->k());
    dht::Passport passport_b(
      this->keys_b->K(), "network-name", this->keys_a->k());
    dht::Passport passport_c(
      this->keys_c->K(), "network-name", this->keys_a->k());
    infinit::overlay::Stonehenge::Peers members;
    members.emplace_back(id_a);
    members.emplace_back(id_b);
    members.emplace_back(id_c);
    std::vector<infinit::overlay::Stonehenge*> stonehenges;
    make_overlay =
      [make_overlay, &stonehenges] (
        int n,
        infinit::model::Address id,
        infinit::overlay::Stonehenge::Peers peers,
        std::shared_ptr<infinit::model::doughnut::Local> local,
        infinit::model::doughnut::Doughnut& d)
      {
        auto res = make_overlay(
          n, std::move(id), std::move(peers), std::move(local), d);
        stonehenges.emplace_back(res.get());
        return res;
      };
    this->dht_a = std::make_shared<dht::Doughnut>(
      id_a,
      this->keys_a,
      this->keys_a->public_key(),
      passport_a,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(
        [=] (infinit::model::doughnut::Doughnut& d,
             infinit::model::Address id,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          return make_overlay(0, id, members, std::move(local), d);
        }),
      boost::optional<int>(),
      std::move(storage_a),
      version_a);
    this->dht_b = std::make_shared<dht::Doughnut>(
      id_b,
      this->keys_b,
      this->keys_a->public_key(),
      passport_b,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(
        [=] (infinit::model::doughnut::Doughnut& d,
             infinit::model::Address id,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          return make_overlay(1, id, members, std::move(local), d);
        }),
      boost::optional<int>(),
      std::move(storage_b),
      version_b);
    this->dht_c = std::make_shared<dht::Doughnut>(
      id_c,
      this->keys_c,
      this->keys_a->public_key(),
      passport_c,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(
        [=] (infinit::model::doughnut::Doughnut& d,
             infinit::model::Address id,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          return make_overlay(2, id, members, std::move(local), d);
        }),
      boost::optional<int>(),
      std::move(storage_c),
      version_c);
    for (auto* stonehenge: stonehenges)
      for (auto& peer: stonehenge->peers())
      {
        elle::unconst(peer).endpoint =
          infinit::overlay::Stonehenge::Peer::Endpoint{"127.0.0.1", 0};
        if (peer.id == id_a)
          elle::unconst(peer).endpoint->port =
            this->dht_a->local()->server_endpoint().port();
        else if (peer.id == id_b)
          elle::unconst(peer).endpoint->port =
            this->dht_b->local()->server_endpoint().port();
        else if (peer.id == id_c)
          elle::unconst(peer).endpoint->port =
            this->dht_c->local()->server_endpoint().port();
        else
          ELLE_ABORT("unknown doughnut id: %s", peer.id);
      }
  }
};

ELLE_TEST_SCHEDULED(CHB, (bool, paxos))
{
  DHTs dhts(paxos);
  auto& dht = *dhts.dht_a;
  {
    elle::Buffer data("\\_o<", 4);
    auto block = dht.make_block<infinit::model::blocks::ImmutableBlock>(data);
    auto addr = block->address();
    dht.store(*block);
    ELLE_LOG("fetch block")
      BOOST_CHECK_EQUAL(dht.fetch(addr)->data(), data);
    ELLE_LOG("remove block")
      dht.remove(addr);
  }
}

ELLE_TEST_SCHEDULED(OKB, (bool, paxos))
{
  DHTs dhts(paxos);
  auto& dht = *dhts.dht_a;
  {
    auto block = dht.make_block<infinit::model::blocks::MutableBlock>();
    elle::Buffer data("\\_o<", 4);
    block->data(elle::Buffer(data));
    auto addr = block->address();
    ELLE_LOG("store mutable block")
    dht.store(*block);
    elle::Buffer updated(">o_/", 4);
    block->data(elle::Buffer(updated));
    ELLE_LOG("fetch block")
    ELLE_ASSERT_EQ(dht.fetch(addr)->data(), data);
    ELLE_LOG("store updated mutable block")
    dht.store(*block);
    ELLE_LOG("fetch block")
    ELLE_ASSERT_EQ(dht.fetch(addr)->data(), updated);
    ELLE_LOG("remove block")
    dht.remove(addr);
  }
}

ELLE_TEST_SCHEDULED(async, (bool, paxos))
{
  DHTs dhts(paxos);
  auto& dht = *dhts.dht_c;
  {
    elle::Buffer data("\\_o<", 4);
    auto block = dht.make_block<infinit::model::blocks::ImmutableBlock>(data);
    std::vector<std::unique_ptr<infinit::model::blocks::ImmutableBlock>> blocks_;
    for (int i = 0; i < 10; ++i)
    {
      auto s = elle::sprintf("\\_o< %d", i);
      elle::Buffer data(elle::sprintf(s).c_str(),
                        (int)std::strlen(s.c_str()));
      blocks_.push_back(
          std::move(dht.make_block<infinit::model::blocks::ImmutableBlock>(data)));
    }
    ELLE_LOG("store block")
    dht.store(*block);
    for (auto& block: blocks_)
      dht.store(*block);
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
    for (auto& block: blocks_)
      dht.fetch(block->address());
    ELLE_LOG("remove block")
      dht.remove(block->address());
  }
  {
    auto block = dht.make_block<infinit::model::blocks::MutableBlock>();
    elle::Buffer data("\\_o<", 4);
    block->data(elle::Buffer(data));
    ELLE_LOG("store block")
    dht.store(*block);
    elle::Buffer updated(">o_/", 4);
    block->data(elle::Buffer(updated));
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
    ELLE_LOG("store block")
    dht.store(*block);
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), updated);
    ELLE_LOG("remove block")
      dht.remove(block->address());
  }
}

ELLE_TEST_SCHEDULED(ACB, (bool, paxos))
{
  DHTs dhts(paxos);
  auto block = dhts.dht_a->make_block<infinit::model::blocks::ACLBlock>();
  elle::Buffer data("\\_o<", 4);
  block->data(elle::Buffer(data));
  ELLE_LOG("owner: store ACB")
  dhts.dht_a->store(*block);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_THROW(fetched->data(), elle::Error);
    auto acb = elle::cast<infinit::model::blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-(", 3));
    ELLE_LOG("other: stored edited ACB")
      BOOST_CHECK_THROW(dhts.dht_b->store(*acb), dht::ValidationFailed);
  }
  ELLE_LOG("owner: add ACB read permissions")
    block->set_permissions(dht::User(dhts.keys_b->K(), ""), true, false);
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->store(*block);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_EQUAL(fetched->data(), "\\_o<");
    auto acb = elle::cast<infinit::model::blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-(", 3));
    ELLE_LOG("other: stored edited ACB")
      BOOST_CHECK_THROW(dhts.dht_b->store(*acb), dht::ValidationFailed);
  }
  ELLE_LOG("owner: add ACB write permissions")
    block->set_permissions(dht::User(dhts.keys_b->K(), ""), true, true);
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->store(*block);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_EQUAL(fetched->data(), "\\_o<");
    auto acb = elle::cast<infinit::model::blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-)", 3));
    ELLE_LOG("other: stored edited ACB")
      dhts.dht_b->store(*acb);
  }
  ELLE_LOG("owner: fetch ACB")
  {
    auto fetched = dhts.dht_a->fetch(block->address());
    BOOST_CHECK_EQUAL(fetched->data(), ":-)");
  }
}

ELLE_TEST_SCHEDULED(NB, (bool, paxos))
{
  DHTs dhts(paxos);
  auto block = elle::make_unique<dht::NB>(
    dhts.dht_a.get(), dhts.keys_a->public_key(), "blockname",
    elle::Buffer("blockdata", 9));
  ELLE_LOG("owner: store NB")
    dhts.dht_a->store(*block);
  {
    ELLE_LOG("other: fetch NB");
    auto fetched =
      dhts.dht_b->fetch(dht::NB::address(dhts.keys_a->K(), "blockname", dhts.dht_b->version()));
    BOOST_CHECK_EQUAL(fetched->data(), "blockdata");
    auto nb = elle::cast<dht::NB>::runtime(fetched);
    BOOST_CHECK(nb);
  }
  { // overwrite
    auto block = elle::make_unique<dht::NB>(
      dhts.dht_a.get(), dhts.keys_a->public_key(), "blockname",
      elle::Buffer("blockdatb", 9));
     BOOST_CHECK_THROW(dhts.dht_a->store(*block), std::exception);
  }
  // remove and remove protection
  BOOST_CHECK_THROW(dhts.dht_a->remove(
    dht::NB::address(dhts.keys_a->K(), "blockname", dhts.dht_a->version()), {}), std::exception);
  BOOST_CHECK_THROW(dhts.dht_b->remove(
    dht::NB::address(dhts.keys_a->K(), "blockname", dhts.dht_b->version())), std::exception);
  dhts.dht_a->remove(dht::NB::address(dhts.keys_a->K(), "blockname", dhts.dht_a->version()));
}

ELLE_TEST_SCHEDULED(UB, (bool, paxos))
{
  DHTs dhts(paxos);
  auto& dhta = dhts.dht_a;
  auto& dhtb = dhts.dht_b;
  {
    dht::UB uba(dhta.get(), "a", dhta->keys().K());
    dht::UB ubarev(dhta.get(), "a", dhta->keys().K(), true);
    dhta->store(uba);
    dhta->store(ubarev);
  }
  auto ruba = dhta->fetch(dht::UB::hash_address(dhta->keys().K(), dhta->version()));
  BOOST_CHECK(ruba);
  auto* uba = dynamic_cast<dht::UB*>(ruba.get());
  BOOST_CHECK(uba);
  dht::UB ubf(dhta.get(), "duck", dhta->keys().K(), true);
  BOOST_CHECK_THROW(dhta->store(ubf), std::exception);
  BOOST_CHECK_THROW(dhtb->store(ubf), std::exception);
  BOOST_CHECK_THROW(dhtb->remove(ruba->address()), std::exception);
  BOOST_CHECK_THROW(dhtb->remove(ruba->address(), {}), std::exception);
  BOOST_CHECK_THROW(dhta->remove(ruba->address(), {}), std::exception);
  dhta->remove(ruba->address());
  dhtb->store(ubf);
}

ELLE_TEST_SCHEDULED(conflict, (bool, paxos))
{
  DHTs dhts(paxos);
  std::unique_ptr<infinit::model::blocks::ACLBlock> block_alice;
  ELLE_LOG("alice: create block")
  {
    block_alice = dhts.dht_a->make_block<infinit::model::blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1", 7));
    block_alice->set_permissions(dht::User(dhts.keys_b->K(), "bob"), true, true);
  }
  ELLE_LOG("alice: store block")
    dhts.dht_a->store(*block_alice);
  std::unique_ptr<
    infinit::model::blocks::ACLBlock,
    std::default_delete<infinit::model::blocks::Block>> block_bob;
  ELLE_LOG("bob: fetch block");
  {
    block_bob = std::static_pointer_cast<infinit::model::blocks::ACLBlock>
      (dhts.dht_b->fetch(block_alice->address()));
    BOOST_CHECK_EQUAL(block_bob->data(), "alice_1");
  }
  ELLE_LOG("alice: modify block")
  {
    block_alice->data(elle::Buffer("alice_2", 7));
    dhts.dht_a->store(*block_alice);
  }
  ELLE_LOG("bob: modify block")
  {
    block_bob->data(elle::Buffer("bob_1", 5));
    BOOST_CHECK_THROW(dhts.dht_b->store(*block_bob),
                      infinit::model::doughnut::Conflict);
  }
}

void
noop(storage::Storage*)
{}

ELLE_TEST_SCHEDULED(restart, (bool, paxos))
{
  auto keys_a = infinit::cryptography::rsa::keypair::generate(512);
  auto keys_b = infinit::cryptography::rsa::keypair::generate(512);
  auto keys_c = infinit::cryptography::rsa::keypair::generate(512);
  auto id_a = infinit::model::Address::random();
  auto id_b = infinit::model::Address::random();
  auto id_c = infinit::model::Address::random();
  storage::Memory::Blocks storage_a;
  storage::Memory::Blocks storage_b;
  storage::Memory::Blocks storage_c;
  // std::unique_ptr<infinit::model::blocks::ImmutableBlock> iblock;
  std::unique_ptr<infinit::model::blocks::MutableBlock> mblock;
  ELLE_LOG("store blocks")
  {
    DHTs dhts(
      paxos,
      keys_a,
      keys_b,
      keys_c,
      id_a,
      id_b,
      id_c,
      elle::make_unique<storage::Memory>(storage_a),
      elle::make_unique<storage::Memory>(storage_b),
      elle::make_unique<storage::Memory>(storage_c)
      );
    // iblock =
    //   dhts.dht_a->make_block<infinit::model::blocks::ImmutableBlock>(
    //     elle::Buffer("immutable", 9));
    // dhts.dht_a->store(*iblock);
    mblock =
      dhts.dht_a->make_block<infinit::model::blocks::MutableBlock>(
        elle::Buffer("mutable", 7));
    dhts.dht_a->store(*mblock);
  }
  ELLE_LOG("load blocks")
  {
    DHTs dhts(
      paxos,
      keys_a,
      keys_b,
      keys_c,
      id_a,
      id_b,
      id_c,
      elle::make_unique<storage::Memory>(storage_a),
      elle::make_unique<storage::Memory>(storage_b),
      elle::make_unique<storage::Memory>(storage_c)
      );
    // auto ifetched = dhts.dht_a->fetch(iblock->address());
    // BOOST_CHECK_EQUAL(iblock->data(), ifetched->data());
    auto mfetched = dhts.dht_a->fetch(mblock->address());
    BOOST_CHECK_EQUAL(mblock->data(), mfetched->data());
  }
}

/*--------------------.
| Paxos: wrong quorum |
`--------------------*/

// Make one of the overlay return a partial quorum, missing one of the three
// members, and check it gets fixed.

class WrongQuorumStonehenge
  : public infinit::overlay::Stonehenge
{
public:
  template <typename ... Args>
  WrongQuorumStonehenge(Args&& ... args)
    : infinit::overlay::Stonehenge(std::forward<Args>(args)...)
    , fail(false)
  {}

  virtual
  reactor::Generator<infinit::overlay::Overlay::Member>
  _lookup(infinit::model::Address address,
          int n,
          infinit::overlay::Operation op) const
  {
    if (fail)
      return infinit::overlay::Stonehenge::_lookup(address, n - 1, op);
    else
      return infinit::overlay::Stonehenge::_lookup(address, n, op);
  }

  bool fail;
};

ELLE_TEST_SCHEDULED(wrong_quorum)
{
  WrongQuorumStonehenge* stonehenge;
  DHTs dhts(
    make_overlay =
    [&stonehenge] (int dht,
                   infinit::model::Address id,
                   infinit::overlay::Stonehenge::Peers peers,
                   std::shared_ptr<infinit::model::doughnut::Local> local,
                   infinit::model::doughnut::Doughnut& d)
    {
      if (dht == 0)
      {
        stonehenge = new WrongQuorumStonehenge(id, peers, std::move(local), &d);
        return std::unique_ptr<infinit::overlay::Stonehenge>(stonehenge);
      }
      else
        return elle::make_unique<infinit::overlay::Stonehenge>(
          id, peers, std::move(local), &d);
    });
  auto block = dhts.dht_a->make_block<infinit::model::blocks::MutableBlock>();
  {
    elle::Buffer data("\\_o<", 4);
    block->data(elle::Buffer(data));
    ELLE_LOG("store block")
      dhts.dht_a->store(*block);
    elle::Buffer updated(">o_/", 4);
    block->data(elle::Buffer(updated));
    stonehenge->fail = true;
    ELLE_LOG("store updated block")
      dhts.dht_a->store(*block);
  }
}

ELLE_TEST_SCHEDULED(cache, (bool, paxos))
{
  dht::consensus::Cache* cache = nullptr;
  DHTs dhts(
    paxos,
    make_consensus =
      [&] (std::unique_ptr<dht::consensus::Consensus> c)
      {
        auto res = elle::make_unique<dht::consensus::Cache>(std::move(c));
        if (!cache)
          cache = res.get();
        return res;
      });
  // Check a null block is never stored in cache.
  {
    auto block = dhts.dht_a->make_block<infinit::model::blocks::MutableBlock>();
    elle::Buffer data("cached", 6);
    block->data(elle::Buffer(data));
    auto addr = block->address();
    ELLE_LOG("store block")
      dhts.dht_a->store(*block);
    auto fetched = [&]
      {
        ELLE_LOG("fetch block")
          return dhts.dht_a->fetch(addr);
      }();
    BOOST_CHECK_EQUAL(block->data(), fetched->data());
    ELLE_LOG("fetch cached block")
      BOOST_CHECK(!dhts.dht_a->fetch(addr, block->version()));
    ELLE_LOG("clear cache")
      cache->clear();
    ELLE_LOG("fetch cached block")
      BOOST_CHECK(!dhts.dht_a->fetch(addr, block->version()));
    ELLE_LOG("fetch block")
      BOOST_CHECK_EQUAL(dhts.dht_a->fetch(addr)->data(), block->data());
  }
}

static std::unique_ptr<infinit::model::blocks::Block>
cycle(infinit::model::doughnut::Doughnut& dht,
      std::unique_ptr<infinit::model::blocks::Block> b)
{
  elle::Buffer buf;
  {
    elle::IOStream os(buf.ostreambuf());
    elle::serialization::binary::SerializerOut sout(os, false);
    sout.set_context(infinit::model::doughnut::ACBDontWaitForSignature{});
    sout.set_context(infinit::model::doughnut::OKBDontWaitForSignature{});
    sout.serialize_forward(b);
  }
  elle::IOStream is(buf.istreambuf());
  elle::serialization::binary::SerializerIn sin(is, false);
  sin.set_context<infinit::model::Model*>(&dht); // FIXME: needed ?
  sin.set_context<infinit::model::doughnut::Doughnut*>(&dht);
  sin.set_context(infinit::model::doughnut::ACBDontWaitForSignature{});
  sin.set_context(infinit::model::doughnut::OKBDontWaitForSignature{});
  auto res = sin.deserialize<std::unique_ptr<infinit::model::blocks::Block>>();
  res->seal();
  return res;
}

ELLE_TEST_SCHEDULED(serialize, (bool, paxos))
{ // test serialization used by async
  DHTs dhts(paxos);
  {
    auto b =  dhts.dht_a->make_block<infinit::model::blocks::ACLBlock>();
    b->data(elle::Buffer("foo"));
    b->seal();
    auto addr = b->address();
    auto cb = cycle(*dhts.dht_a, std::move(b));
    dhts.dht_a->store(std::move(cb));
    cb = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(cb->data(), elle::Buffer("foo"));
  }
  { // wait for signature
    auto b =  dhts.dht_a->make_block<infinit::model::blocks::ACLBlock>();
    b->data(elle::Buffer("foo"));
    b->seal();
    reactor::sleep(100_ms);
    auto addr = b->address();
    auto cb = cycle(*dhts.dht_a, std::move(b));
    dhts.dht_a->store(std::move(cb));
    cb = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(cb->data(), elle::Buffer("foo"));
  }
  { // block we dont own
    auto block_alice = dhts.dht_a->make_block<infinit::model::blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1", 7));
    block_alice->set_permissions(dht::User(dhts.keys_b->K(), "bob"), true, true);
    auto addr = block_alice->address();
    dhts.dht_a->store(std::move(block_alice));
    auto block_bob = dhts.dht_b->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("alice_1"));
    dynamic_cast<infinit::model::blocks::MutableBlock*>(block_bob.get())->data(
      elle::Buffer("bob_1"));
    block_bob->seal();
    block_bob = cycle(*dhts.dht_b, std::move(block_bob));
    dhts.dht_b->store(std::move(block_bob));
    block_bob = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("bob_1"));
  }
  { // signing with group key
    std::unique_ptr<infinit::cryptography::rsa::PublicKey> gkey;
    {
      infinit::model::doughnut::Group g(*dhts.dht_a, "g");
      g.create();
      g.add_member(dht::User(dhts.keys_b->K(), "bob"));
      gkey.reset(new infinit::cryptography::rsa::PublicKey(g.public_control_key()));
    }
    auto block_alice = dhts.dht_a->make_block<infinit::model::blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1", 7));
    block_alice->set_permissions(dht::User(*gkey, "@g"), true, true);
    auto addr = block_alice->address();
    dhts.dht_a->store(std::move(block_alice));
    auto block_bob = dhts.dht_b->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("alice_1"));
    dynamic_cast<infinit::model::blocks::MutableBlock*>(block_bob.get())->data(
      elle::Buffer("bob_1"));
    block_bob->seal();
    block_bob = cycle(*dhts.dht_b, std::move(block_bob));
    dhts.dht_b->store(std::move(block_bob));
    block_bob = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("bob_1"));
  }
}

ELLE_TEST_SCHEDULED(flags_backward, (bool, paxos))
{
  auto keys_a = infinit::cryptography::rsa::keypair::generate(512);
  auto keys_b = infinit::cryptography::rsa::keypair::generate(512);
  auto keys_c = infinit::cryptography::rsa::keypair::generate(512);
  auto id_a = infinit::model::Address::random();
  auto id_b = infinit::model::Address::random();
  auto id_c = infinit::model::Address::random();
  storage::Memory::Blocks blocks_a;
  storage::Memory::Blocks blocks_b;
  storage::Memory::Blocks blocks_c;
  infinit::model::Address chbaddr;
  infinit::model::Address acbaddr;
  elle::Buffer data("\\_o<", 4);
  {
    DHTs dhts(
      paxos,
      keys_a,
      keys_b,
      keys_c,
      id_a,
      id_b,
      id_c,
      elle::make_unique<storage::Memory>(blocks_a),
      elle::make_unique<storage::Memory>(blocks_b),
      elle::make_unique<storage::Memory>(blocks_c),
      elle::Version(0, 4, 0),
      elle::Version(0, 4, 0),
      elle::Version(0, 4, 0)
      );
    auto& dhta = dhts.dht_a;
    auto& dhtb = dhts.dht_b;
    // UB
    ELLE_LOG("store UB")
    {
      dht::UB uba(dhta.get(), "a", dhta->keys().K());
      dht::UB ubarev(dhta.get(), "a", dhta->keys().K(), true);
      dhta->store(uba);
      dhta->store(ubarev);
      auto ruba = dhta->fetch(dht::UB::hash_address(dhta->keys().K(), dhta->version()));
      BOOST_CHECK(ruba);
    }
    // NB
    ELLE_LOG("store NB")
    {
      auto nb = elle::make_unique<dht::NB>(
        dhta.get(), dhts.keys_a->public_key(), "blockname",
        elle::Buffer("blockdata", 9));
      dhta->store(*nb);
      auto fetched =
        dhtb->fetch(dht::NB::address(dhts.keys_a->K(), "blockname", dhts.dht_b->version()));
      BOOST_CHECK_EQUAL(fetched->data(), "blockdata");
    }
    // CHB
    ELLE_LOG("store CHB")
    {
      auto chb = dhta->make_block<infinit::model::blocks::ImmutableBlock>(data);
      chbaddr = chb->address();
      dhta->store(*chb);
      BOOST_CHECK_EQUAL(dhta->fetch(chbaddr)->data(), data);
    }
    // ACB
    ELLE_LOG("store ACB")
    {
      auto acb = dhta->make_block<infinit::model::blocks::ACLBlock>();
      acb->data(elle::Buffer(data));
      dhta->store(*acb);
      acbaddr = acb->address();
      auto fetched = dhta->fetch(acb->address());
      BOOST_CHECK_EQUAL(fetched->data(), data);
    }
  }
  {
    DHTs dhts(
      paxos,
      keys_a,
      keys_b,
      keys_c,
      id_a,
      id_b,
      id_c,
      elle::make_unique<storage::Memory>(blocks_a),
      elle::make_unique<storage::Memory>(blocks_b),
      elle::make_unique<storage::Memory>(blocks_c),
      elle::Version(0, 5, 0),
      elle::Version(0, 5, 0),
      elle::Version(0, 5, 0));
    auto& dhta = dhts.dht_a;
    auto& dhtb = dhts.dht_b;
    // UB
    ELLE_LOG("fetch UB with wrong address")
    {
      std::unique_ptr<infinit::model::blocks::Block> ruba;
      BOOST_CHECK_NO_THROW(
        ruba = dhta->fetch(dht::UB::hash_address(dhta->keys().K(), dhta->version())));
      BOOST_CHECK(ruba);
    }
    // NB
    ELLE_LOG("fetch NB with wrong address")
    {
      std::unique_ptr<infinit::model::blocks::Block> fetched;
      BOOST_CHECK_NO_THROW(
        fetched =
        dhtb->fetch(
          dht::NB::address(dhts.keys_a->K(), "blockname",
                           dhts.dht_b->version())));
      BOOST_CHECK_EQUAL(fetched->data(), "blockdata");
    }
    // CHB
    ELLE_LOG("fetch CHB with wrong address")
    {
      std::unique_ptr<infinit::model::blocks::Block> fetched;
      BOOST_CHECK_EQUAL(dhta->fetch(chbaddr)->data(), data);
      infinit::model::Address addr(
        chbaddr.value(), infinit::model::flags::immutable_block);
      BOOST_CHECK_EQUAL(dhta->fetch(addr)->data(), data);
    }
    // ACB
    ELLE_LOG("fetch ACB with wrong address")
    {
      std::unique_ptr<infinit::model::blocks::Block> fetched;
      BOOST_CHECK_NO_THROW(fetched = dhta->fetch(acbaddr));
      BOOST_CHECK_EQUAL(fetched->data(), data);
      BOOST_CHECK_NO_THROW(fetched = dhta->fetch(infinit::model::Address(acbaddr.value(), infinit::model::flags::mutable_block)));
      BOOST_CHECK_EQUAL(fetched->data(), data);
    }
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  boost::unit_test::test_suite* plain = BOOST_TEST_SUITE("plain");
  suite.add(plain);
  boost::unit_test::test_suite* paxos = BOOST_TEST_SUITE("paxos");
  suite.add(paxos);
#define TEST(Name)                              \
  {                                             \
    auto Name = boost::bind(::Name, true);      \
    paxos->add(BOOST_TEST_CASE(Name));          \
  }                                             \
  {                                             \
    auto Name = boost::bind(::Name, false);     \
    plain->add(BOOST_TEST_CASE(Name));          \
  }
  TEST(CHB);
  TEST(OKB);
  TEST(async);
  TEST(ACB);
  TEST(NB);
  TEST(UB);
  TEST(conflict);
  TEST(restart);
  TEST(cache);
  TEST(serialize);
  TEST(flags_backward);
#undef TEST
  paxos->add(BOOST_TEST_CASE(wrong_quorum));
}
