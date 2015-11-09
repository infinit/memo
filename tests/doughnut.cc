#include <memory>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/named.hh>
#include <elle/test.hh>
#include <elle/utils.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/storage/Memory.hh>

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

class DHTs
{
public:
  template <typename ... Args>
  DHTs(Args&& ... args)
  {
    namespace ph = std::placeholders;
    elle::named::prototype(
      paxos = true,
      ::keys_a = infinit::cryptography::rsa::keypair::generate(2048),
      ::keys_b = infinit::cryptography::rsa::keypair::generate(2048),
      ::keys_c = infinit::cryptography::rsa::keypair::generate(2048),
      id_a = infinit::model::Address::random(),
      id_b = infinit::model::Address::random(),
      id_c = infinit::model::Address::random(),
      storage_a = nullptr,
      storage_b = nullptr,
      storage_c = nullptr,
      make_overlay =
      [] (int,
          infinit::model::Address id,
          infinit::overlay::Stonehenge::Peers peers,
          infinit::model::doughnut::Doughnut& d,
          bool server)
      {
        return elle::make_unique<infinit::overlay::Stonehenge>(id, peers, &d);
      }).call([this] (bool paxos,
                      infinit::cryptography::rsa::KeyPair keys_a,
                      infinit::cryptography::rsa::KeyPair keys_b,
                      infinit::cryptography::rsa::KeyPair keys_c,
                      infinit::model::Address id_a,
                      infinit::model::Address id_b,
                      infinit::model::Address id_c,
                      std::unique_ptr<storage::Storage> storage_a,
                      std::unique_ptr<storage::Storage> storage_b,
                      std::unique_ptr<storage::Storage> storage_c,
                      std::function<
                      std::unique_ptr<infinit::overlay::Stonehenge>(
                        int,
                        infinit::model::Address id,
                        infinit::overlay::Stonehenge::Peers peers,
                        infinit::model::doughnut::Doughnut& d,
                        bool server)> make_overlay)
              {
                this-> init(paxos,
                            std::move(keys_a),
                            std::move(keys_b),
                            std::move(keys_c),
                            id_a, id_b, id_c,
                            std::move(storage_a),
                            std::move(storage_b),
                            std::move(storage_c) ,
                            std::move(make_overlay));
              }, std::forward<Args>(args)...);
  }

  std::unique_ptr<infinit::cryptography::rsa::KeyPair> keys_a;
  std::unique_ptr<infinit::cryptography::rsa::KeyPair> keys_b;
  std::unique_ptr<infinit::cryptography::rsa::KeyPair> keys_c;
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
       std::function<
         std::unique_ptr<infinit::overlay::Stonehenge>(
           int,
           infinit::model::Address id,
           infinit::overlay::Stonehenge::Peers peers,
           infinit::model::doughnut::Doughnut& d,
           bool server)> make_overlay)
  {
    this->keys_a =
      elle::make_unique<infinit::cryptography::rsa::KeyPair>(std::move(keys_a));
    this->keys_b =
      elle::make_unique<infinit::cryptography::rsa::KeyPair>(std::move(keys_b));
    this->keys_c =
      elle::make_unique<infinit::cryptography::rsa::KeyPair>(std::move(keys_c));
    if (!storage_a)
      storage_a = elle::make_unique<storage::Memory>();
    if (!storage_b)
      storage_b = elle::make_unique<storage::Memory>();
    if (!storage_c)
      storage_c = elle::make_unique<storage::Memory>();
    dht::Doughnut::ConsensusBuilder consensus;
    if (paxos)
      consensus = [&] (dht::Doughnut& dht)
        { return elle::make_unique<dht::consensus::Paxos>(dht, 3); };
    else
      consensus = [&] (dht::Doughnut& dht)
        { return elle::make_unique<dht::consensus::Consensus>(dht); };
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
      [make_overlay, &stonehenges] (int n,
                                    infinit::model::Address id,
                                    infinit::overlay::Stonehenge::Peers peers,
                                    infinit::model::doughnut::Doughnut& d,
                                    bool server)
      {
        auto res = make_overlay(n, std::move(id), std::move(peers), d, server);
        stonehenges.emplace_back(res.get());
        return res;
      };
    this->dht_a = std::make_shared<dht::Doughnut>(
      *this->keys_a,
      this->keys_a->K(),
      passport_a,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(
        [=] (infinit::model::doughnut::Doughnut& d, bool server)
        {
          return make_overlay(0, id_a, members, d, server);
        }),
      boost::optional<int>(),
      std::move(storage_a));
    this->dht_b = std::make_shared<dht::Doughnut>(
      *this->keys_b,
      this->keys_a->K(),
      passport_b,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(
        [=] (infinit::model::doughnut::Doughnut& d, bool server)
        {
          return make_overlay(1, id_b, members, d, server);
        }),
      boost::optional<int>(),
      std::move(storage_b));
    this->dht_c = std::make_shared<dht::Doughnut>(
      *this->keys_c,
      this->keys_a->K(),
      passport_c,
      consensus,
      infinit::model::doughnut::Doughnut::OverlayBuilder(
        [=] (infinit::model::doughnut::Doughnut& d, bool server)
        {
          return make_overlay(2, id_b, members, d, server);
        }),
      boost::optional<int>(),
      std::move(storage_c));
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
  DHTs dhts(::paxos = paxos);
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
  DHTs dhts(::paxos = paxos);
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
  DHTs dhts(::paxos = paxos);
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
  DHTs dhts(::paxos = paxos);
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
  DHTs dhts(::paxos = paxos);
  auto block = elle::make_unique<dht::NB>(
    dhts.dht_a.get(), dhts.keys_a->K(), "blockname",
    elle::Buffer("blockdata", 9));
  ELLE_LOG("owner: store NB")
    dhts.dht_a->store(*block);
  {
    ELLE_LOG("other: fetch NB");
    auto fetched =
      dhts.dht_b->fetch(dht::NB::address(dhts.keys_a->K(), "blockname"));
    BOOST_CHECK_EQUAL(fetched->data(), "blockdata");
    auto nb = elle::cast<dht::NB>::runtime(fetched);
    BOOST_CHECK(nb);
  }
}

ELLE_TEST_SCHEDULED(conflict, (bool, paxos))
{
  DHTs dhts(::paxos = paxos);
  std::unique_ptr<infinit::model::blocks::ACLBlock> block_alice;
  ELLE_LOG("alice: create block")
  {
    block_alice = dhts.dht_a->make_block<infinit::model::blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1", 7));
    block_alice->set_permissions(dht::User(dhts.keys_b->K(), "bob"), true, true);
  }
  ELLE_LOG("alice: store block")
    dhts.dht_a->store(*block_alice);
  std::unique_ptr<infinit::model::blocks::ACLBlock> block_bob;
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
  auto keys_a = infinit::cryptography::rsa::keypair::generate(2048);
  auto keys_b = infinit::cryptography::rsa::keypair::generate(2048);
  auto keys_c = infinit::cryptography::rsa::keypair::generate(2048);
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
      ::paxos = paxos,
      ::keys_a = keys_a,
      ::keys_b = keys_b,
      ::keys_c = keys_c,
      ::id_a = id_a,
      ::id_b = id_b,
      ::id_c = id_c,
      ::storage_a = elle::make_unique<storage::Memory>(storage_a),
      ::storage_b = elle::make_unique<storage::Memory>(storage_b),
      ::storage_c = elle::make_unique<storage::Memory>(storage_c)
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
      ::paxos = paxos,
      ::keys_a = keys_a,
      ::keys_b = keys_b,
      ::keys_c = keys_c,
      ::id_a = id_a,
      ::id_b = id_b,
      ::id_c = id_c,
      ::storage_a = elle::make_unique<storage::Memory>(storage_a),
      ::storage_b = elle::make_unique<storage::Memory>(storage_b),
      ::storage_c = elle::make_unique<storage::Memory>(storage_c)
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
                   infinit::model::doughnut::Doughnut& d,
                   bool)
    {
      if (dht == 0)
      {
        stonehenge = new WrongQuorumStonehenge(id, peers, &d);
        return std::unique_ptr<infinit::overlay::Stonehenge>(stonehenge);
      }
      else
        return elle::make_unique<infinit::overlay::Stonehenge>(id, peers, &d);
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
  TEST(conflict);
  TEST(restart);
#undef TEST
  paxos->add(BOOST_TEST_CASE(wrong_quorum));
}
