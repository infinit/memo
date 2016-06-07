#include <memory>

#include <boost/signals2/connection.hpp>

#include <elle/cast.hh>
#include <elle/log.hh>
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

#include "DHT.hh"

ELLE_LOG_COMPONENT("infinit.model.doughnut.test");

namespace blocks = infinit::model::blocks;
namespace dht = infinit::model::doughnut;
using namespace infinit::storage;

NAMED_ARGUMENT(keys_a);
NAMED_ARGUMENT(keys_b);
NAMED_ARGUMENT(keys_c);
NAMED_ARGUMENT(id_a);
NAMED_ARGUMENT(id_b);
NAMED_ARGUMENT(id_c);
NAMED_ARGUMENT(storage_a);
NAMED_ARGUMENT(storage_b);
NAMED_ARGUMENT(storage_c);
NAMED_ARGUMENT(version_a);
NAMED_ARGUMENT(version_b);
NAMED_ARGUMENT(version_c);

static
int
key_size()
{
  return RUNNING_ON_VALGRIND ? 512 : 2048;
}

class DHTs
{
public:
  template <typename ... Args>
  DHTs(Args&& ... args)
  {
    namespace ph = std::placeholders;
    elle::named::prototype(
      paxos = true,
      ::keys_a = infinit::cryptography::rsa::keypair::generate(key_size()),
      ::keys_b = infinit::cryptography::rsa::keypair::generate(key_size()),
      ::keys_c = infinit::cryptography::rsa::keypair::generate(key_size()),
      id_a = infinit::model::Address::random(0), // FIXME
      id_b = infinit::model::Address::random(0), // FIXME
      id_c = infinit::model::Address::random(0), // FIXME
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
                      std::unique_ptr<Storage> storage_a,
                      std::unique_ptr<Storage> storage_b,
                      std::unique_ptr<Storage> storage_c,
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
       std::unique_ptr<Storage> storage_a,
       std::unique_ptr<Storage> storage_b,
       std::unique_ptr<Storage> storage_c,
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
      storage_a = elle::make_unique<Memory>();
    if (!storage_b)
      storage_b = elle::make_unique<Memory>();
    if (!storage_c)
      storage_c = elle::make_unique<Memory>();
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
      this->keys_a->K(), "network-name", *this->keys_a);
    dht::Passport passport_b(
      this->keys_b->K(), "network-name", *this->keys_a);
    dht::Passport passport_c(
      this->keys_c->K(), "network-name", *this->keys_a);
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
    auto block = dht.make_block<blocks::ImmutableBlock>(data);
    auto addr = block->address();
    ELLE_LOG("store block")
      dht.store(*block, infinit::model::STORE_INSERT);
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
    auto block = dht.make_block<blocks::MutableBlock>();
    elle::Buffer data("\\_o<", 4);
    block->data(elle::Buffer(data));
    auto addr = block->address();
    ELLE_LOG("store mutable block")
      dht.store(*block, infinit::model::STORE_INSERT);
    elle::Buffer updated(">o_/", 4);
    block->data(elle::Buffer(updated));
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(addr)->data(), data);
    ELLE_LOG("store updated mutable block")
      dht.store(*block, infinit::model::STORE_UPDATE);
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
    auto block = dht.make_block<blocks::ImmutableBlock>(data);
    std::vector<std::unique_ptr<blocks::ImmutableBlock>> blocks_;
    for (int i = 0; i < 10; ++i)
    {
      auto s = elle::sprintf("\\_o< %d", i);
      elle::Buffer data(elle::sprintf(s).c_str(),
                        (int)std::strlen(s.c_str()));
      blocks_.push_back(dht.make_block<blocks::ImmutableBlock>(data));
    }
    ELLE_LOG("store block")
      dht.store(*block, infinit::model::STORE_INSERT);
    for (auto& block: blocks_)
      dht.store(*block, infinit::model::STORE_INSERT);
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
    for (auto& block: blocks_)
      dht.fetch(block->address());
    ELLE_LOG("remove block")
      dht.remove(block->address());
  }
  {
    auto block = dht.make_block<blocks::MutableBlock>();
    elle::Buffer data("\\_o<", 4);
    block->data(elle::Buffer(data));
    ELLE_LOG("store block")
      dht.store(*block, infinit::model::STORE_INSERT);
    elle::Buffer updated(">o_/", 4);
    block->data(elle::Buffer(updated));
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
    ELLE_LOG("store block")
      dht.store(*block, infinit::model::STORE_UPDATE);
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), updated);
    ELLE_LOG("remove block")
      dht.remove(block->address());
  }
}

ELLE_TEST_SCHEDULED(ACB, (bool, paxos))
{
  DHTs dhts(paxos);
  auto block = dhts.dht_a->make_block<blocks::ACLBlock>();
  elle::Buffer data("\\_o<", 4);
  block->data(elle::Buffer(data));
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->store(*block, infinit::model::STORE_INSERT);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_THROW(fetched->data(), elle::Error);
    auto acb = elle::cast<blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-(", 3));
    ELLE_LOG("other: stored edited ACB")
      BOOST_CHECK_THROW(dhts.dht_b->store(*acb, infinit::model::STORE_UPDATE),
                        dht::ValidationFailed);
  }
  ELLE_LOG("owner: add ACB read permissions")
    block->set_permissions(dht::User(dhts.keys_b->K(), ""), true, false);
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->store(*block, infinit::model::STORE_UPDATE);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_EQUAL(fetched->data(), "\\_o<");
    auto acb = elle::cast<blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-(", 3));
    ELLE_LOG("other: stored edited ACB")
      BOOST_CHECK_THROW(dhts.dht_b->store(*acb, infinit::model::STORE_UPDATE),
                        dht::ValidationFailed);
  }
  ELLE_LOG("owner: add ACB write permissions")
    block->set_permissions(dht::User(dhts.keys_b->K(), ""), true, true);
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->store(*block, infinit::model::STORE_UPDATE);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_EQUAL(fetched->data(), "\\_o<");
    auto acb = elle::cast<blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-)", 3));
    ELLE_LOG("other: stored edited ACB")
      dhts.dht_b->store(*acb, infinit::model::STORE_UPDATE);
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
    dhts.dht_a->store(*block, infinit::model::STORE_INSERT);
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
    BOOST_CHECK_THROW(dhts.dht_a->store(*block, infinit::model::STORE_UPDATE),
                      std::exception);
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
  ELLE_LOG("store UB and RUB")
  {
    dht::UB uba(dhta.get(), "a", dhta->keys().K());
    dht::UB ubarev(dhta.get(), "a", dhta->keys().K(), true);
    dhta->store(uba, infinit::model::STORE_INSERT);
    dhta->store(ubarev, infinit::model::STORE_INSERT);
  }
  auto ruba = dhta->fetch(dht::UB::hash_address(dhta->keys().K(), *dhta));
  BOOST_CHECK(ruba);
  auto* uba = dynamic_cast<dht::UB*>(ruba.get());
  BOOST_CHECK(uba);
  dht::UB ubf(dhta.get(), "duck", dhta->keys().K(), true);
  ELLE_LOG("fail storing different UB")
  {
    BOOST_CHECK_THROW(dhta->store(ubf, infinit::model::STORE_INSERT),
                    std::exception);
    BOOST_CHECK_THROW(dhtb->store(ubf, infinit::model::STORE_INSERT),
                      std::exception);
  }
  ELLE_LOG("fail removing RUB")
  {
    BOOST_CHECK_THROW(dhtb->remove(ruba->address()), std::exception);
    BOOST_CHECK_THROW(dhtb->remove(ruba->address(), {}), std::exception);
    BOOST_CHECK_THROW(dhta->remove(ruba->address(), {}), std::exception);
  }
  ELLE_LOG("remove RUB")
    dhta->remove(ruba->address());
  ELLE_LOG("store different UB")
    dhtb->store(ubf, infinit::model::STORE_INSERT);
}

namespace removal
{
  ELLE_TEST_SCHEDULED(serialize_ACB_remove, (bool, paxos))
  {
    Memory::Blocks dht_storage;
    auto dht_id = infinit::model::Address::random();
    infinit::model::Address address;
    // Store signature removal in the first run so the second run of the DHT
    // does not fetch the block before removing it. This tests the block is
    // still reloaded without a previous fetch.
    elle::Buffer rs_bad;
    elle::Buffer rs_good;
    ELLE_LOG("store block")
    {
      DHT dht(id = dht_id,
              storage = elle::make_unique<Memory>(dht_storage));
      auto b = dht.dht->make_block<blocks::ACLBlock>();
      address = b->address();
      b->data(std::string("removal/serialize_ACB_remove"));
      dht.dht->store(*b, infinit::model::STORE_INSERT);
      rs_bad =
        elle::serialization::binary::serialize(b->sign_remove(*dht.dht));
      b->data([] (elle::Buffer& b) { b.append("_", 1); });
      dht.dht->store(*b, infinit::model::STORE_UPDATE);
      rs_good =
        elle::serialization::binary::serialize(b->sign_remove(*dht.dht));
    }
    ELLE_LOG("fail removing block")
    {
      DHT dht(id = dht_id,
              storage = elle::make_unique<Memory>(dht_storage));
      elle::serialization::Context ctx;
      ctx.set<infinit::model::doughnut::Doughnut*>(dht.dht.get());
      auto sig = elle::serialization::binary::deserialize<
        blocks::RemoveSignature>(rs_bad, true, ctx);
      BOOST_CHECK_THROW(
        dht.dht->remove(address, sig), infinit::model::doughnut::Conflict);
    }
    ELLE_LOG("remove block")
    {
      DHT dht(id = dht_id,
              storage = elle::make_unique<Memory>(dht_storage));
      elle::serialization::Context ctx;
      ctx.set<infinit::model::doughnut::Doughnut*>(dht.dht.get());
      auto sig = elle::serialization::binary::deserialize<
        blocks::RemoveSignature>(rs_good, true, ctx);
      dht.dht->remove(address, sig);
    }
    BOOST_CHECK(!contains(dht_storage, address));
  }
}

class AppendConflictResolver
  : public infinit::model::ConflictResolver
{
  virtual
  std::unique_ptr<blocks::Block>
  operator () (blocks::Block& old,
               blocks::Block& current,
               infinit::model::StoreMode mode) override
  {
    auto res = std::dynamic_pointer_cast<blocks::MutableBlock>(current.clone());
    res->data([] (elle::Buffer& data) { data.append("B", 1); });
    return std::unique_ptr<blocks::Block>(res.release());
  }

  virtual
  void
  serialize(elle::serialization::Serializer& s,
            elle::Version const&) override
  {}
};

ELLE_TEST_SCHEDULED(conflict, (bool, paxos))
{
  DHTs dhts(paxos);
  std::unique_ptr<blocks::ACLBlock> block_alice;
  ELLE_LOG("alice: create block")
  {
    block_alice = dhts.dht_a->make_block<blocks::ACLBlock>();
    block_alice->data(elle::Buffer("A", 1));
    block_alice->set_permissions(dht::User(dhts.keys_b->K(), "bob"), true, true);
  }
  ELLE_LOG("alice: store block")
    dhts.dht_a->store(*block_alice, infinit::model::STORE_INSERT);
  std::unique_ptr<
    blocks::ACLBlock,
    std::default_delete<blocks::Block>> block_bob;
  ELLE_LOG("bob: fetch block");
  {
    block_bob = std::static_pointer_cast<blocks::ACLBlock>
      (dhts.dht_b->fetch(block_alice->address()));
    BOOST_CHECK_EQUAL(block_bob->data(), "A");
  }
  ELLE_LOG("alice: modify block")
  {
    block_alice->data(elle::Buffer("AA", 2));
    dhts.dht_a->store(*block_alice, infinit::model::STORE_UPDATE);
  }
  ELLE_LOG("bob: modify block")
  {
    block_bob->data(elle::Buffer("AB", 2));
    BOOST_CHECK_THROW(
      dhts.dht_b->store(*block_bob, infinit::model::STORE_UPDATE),
      infinit::model::doughnut::Conflict);
    dhts.dht_b->store(*block_bob, infinit::model::STORE_UPDATE,
                      elle::make_unique<AppendConflictResolver>());
  }
  ELLE_LOG("alice: fetch block")
  {
    BOOST_CHECK_EQUAL(
      dhts.dht_a->fetch(block_alice->address())->data(), "AAB");
  }
}

void
noop(Storage*)
{}

ELLE_TEST_SCHEDULED(restart, (bool, paxos))
{
  auto keys_a = infinit::cryptography::rsa::keypair::generate(key_size());
  auto keys_b = infinit::cryptography::rsa::keypair::generate(key_size());
  auto keys_c = infinit::cryptography::rsa::keypair::generate(key_size());
  auto id_a = infinit::model::Address::random(0); // FIXME
  auto id_b = infinit::model::Address::random(0); // FIXME
  auto id_c = infinit::model::Address::random(0); // FIXME
  Memory::Blocks storage_a;
  Memory::Blocks storage_b;
  Memory::Blocks storage_c;
  // std::unique_ptr<blocks::ImmutableBlock> iblock;
  std::unique_ptr<blocks::MutableBlock> mblock;
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
      elle::make_unique<Memory>(storage_a),
      elle::make_unique<Memory>(storage_b),
      elle::make_unique<Memory>(storage_c)
      );
    // iblock =
    //   dhts.dht_a->make_block<blocks::ImmutableBlock>(
    //     elle::Buffer("immutable", 9));
    // dhts.dht_a->store(*iblock);
    mblock =
      dhts.dht_a->make_block<blocks::MutableBlock>(
        elle::Buffer("mutable", 7));
    dhts.dht_a->store(*mblock, infinit::model::STORE_INSERT);
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
      elle::make_unique<Memory>(storage_a),
      elle::make_unique<Memory>(storage_b),
      elle::make_unique<Memory>(storage_c)
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
  reactor::Generator<infinit::overlay::Overlay::WeakMember>
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
  auto block = dhts.dht_a->make_block<blocks::MutableBlock>();
  {
    elle::Buffer data("\\_o<", 4);
    block->data(elle::Buffer(data));
    ELLE_LOG("store block")
      dhts.dht_a->store(*block, infinit::model::STORE_INSERT);
    elle::Buffer updated(">o_/", 4);
    block->data(elle::Buffer(updated));
    stonehenge->fail = true;
    ELLE_LOG("store updated block")
      dhts.dht_a->store(*block, infinit::model::STORE_UPDATE);
  }
}

namespace tests_paxos
{
  ELLE_TEST_SCHEDULED(CHB_no_peer)
  {
    DHT dht(storage = nullptr);
    auto chb = dht.dht->make_block<blocks::ImmutableBlock>();
    BOOST_CHECK_THROW(dht.dht->store(*chb, infinit::model::STORE_INSERT),
                      elle::Error);
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
    auto block = dhts.dht_a->make_block<blocks::MutableBlock>();
    elle::Buffer data("cached", 6);
    block->data(elle::Buffer(data));
    auto addr = block->address();
    ELLE_LOG("store block")
      dhts.dht_a->store(*block, infinit::model::STORE_INSERT);
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

static std::unique_ptr<blocks::Block>
cycle(infinit::model::doughnut::Doughnut& dht,
      std::unique_ptr<blocks::Block> b)
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
  auto res = sin.deserialize<std::unique_ptr<blocks::Block>>();
  res->seal();
  return res;
}

ELLE_TEST_SCHEDULED(serialize, (bool, paxos))
{ // test serialization used by async
  DHTs dhts(paxos);
  {
    auto b =  dhts.dht_a->make_block<blocks::ACLBlock>();
    b->data(elle::Buffer("foo"));
    b->seal();
    auto addr = b->address();
    auto cb = cycle(*dhts.dht_a, std::move(b));
    dhts.dht_a->store(std::move(cb), infinit::model::STORE_INSERT);
    cb = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(cb->data(), elle::Buffer("foo"));
  }
  { // wait for signature
    auto b =  dhts.dht_a->make_block<blocks::ACLBlock>();
    b->data(elle::Buffer("foo"));
    b->seal();
    reactor::sleep(100_ms);
    auto addr = b->address();
    auto cb = cycle(*dhts.dht_a, std::move(b));
    dhts.dht_a->store(std::move(cb), infinit::model::STORE_INSERT);
    cb = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(cb->data(), elle::Buffer("foo"));
  }
  { // block we dont own
    auto block_alice = dhts.dht_a->make_block<blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1", 7));
    block_alice->set_permissions(dht::User(dhts.keys_b->K(), "bob"), true, true);
    auto addr = block_alice->address();
    dhts.dht_a->store(std::move(block_alice), infinit::model::STORE_INSERT);
    auto block_bob = dhts.dht_b->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("alice_1"));
    dynamic_cast<blocks::MutableBlock*>(block_bob.get())->data(
      elle::Buffer("bob_1"));
    block_bob->seal();
    block_bob = cycle(*dhts.dht_b, std::move(block_bob));
    dhts.dht_b->store(std::move(block_bob), infinit::model::STORE_UPDATE);
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
    auto block_alice = dhts.dht_a->make_block<blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1", 7));
    block_alice->set_permissions(dht::User(*gkey, "@g"), true, true);
    auto addr = block_alice->address();
    dhts.dht_a->store(std::move(block_alice), infinit::model::STORE_INSERT);
    auto block_bob = dhts.dht_b->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("alice_1"));
    dynamic_cast<blocks::MutableBlock*>(block_bob.get())->data(
      elle::Buffer("bob_1"));
    block_bob->seal();
    block_bob = cycle(*dhts.dht_b, std::move(block_bob));
    dhts.dht_b->store(std::move(block_bob), infinit::model::STORE_UPDATE);
    block_bob = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("bob_1"));
  }
}

template <typename T>
int
size(reactor::Generator<T> const& g)
{
  int res = 0;
  for (auto const& m: elle::unconst(g))
  {
    (void)m;
    ++res;
  }
  return res;
};

namespace rebalancing
{
  ELLE_TEST_SCHEDULED(extend_and_write)
  {
    DHT dht_a(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    DHT dht_b(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    auto b1 = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 1")
    {
      b1->data(std::string("extend_and_write 1"));
      dht_a.dht->store(*b1, infinit::model::STORE_INSERT);
    }
    dht_b.overlay->connect(*dht_a.overlay);
    auto op = infinit::overlay::OP_FETCH;
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b1->address(), 3, op)), 1u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b1->address(), 3, op)), 1u);
    auto& paxos_a =
      dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
    ELLE_LOG("rebalance block to quorum of 2")
      paxos_a.rebalance(b1->address());
    ELLE_LOG("write block to quorum of 2")
    {
      b1->data(std::string("extend_and_write 1 bis"));
      dht_a.dht->store(*b1, infinit::model::STORE_UPDATE);
    }
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b1->address(), 3, op)), 2u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b1->address(), 3, op)), 2u);
  }

  ELLE_TEST_SCHEDULED(shrink_and_write)
  {
    DHT dht_a(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    DHT dht_b(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    dht_b.overlay->connect(*dht_a.overlay);
    auto b1 = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 2")
    {
      b1->data(std::string("shrink_kill_and_write 1"));
      dht_a.dht->store(*b1, infinit::model::STORE_INSERT);
    }
    auto& paxos_a =
      dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
    ELLE_LOG("rebalance block to quorum of 1")
      paxos_a.rebalance(b1->address(), {dht_a.dht->id()});
    ELLE_LOG("write block to quorum of 1")
    {
      b1->data(std::string("extend_and_write 2"));
      dht_a.dht->store(*b1, infinit::model::STORE_UPDATE);
    }
  }

  ELLE_TEST_SCHEDULED(shrink_kill_and_write)
  {
    DHT dht_a(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    DHT dht_b(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    dht_b.overlay->connect(*dht_a.overlay);
    auto b1 = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 2")
    {
      b1->data(std::string("shrink_kill_and_write 1"));
      dht_a.dht->store(*b1, infinit::model::STORE_INSERT);
    }
    auto& paxos_a =
      dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
    ELLE_LOG("rebalance block to quorum of 1")
      paxos_a.rebalance(b1->address(), {dht_a.dht->id()});
    dht_b.overlay->disconnect(*dht_a.overlay);
    ELLE_LOG("write block to quorum of 1")
    {
      b1->data(std::string("extend_and_write 2"));
      dht_a.dht->store(*b1, infinit::model::STORE_UPDATE);
    }
  }

  ELLE_TEST_SCHEDULED(quorum_duel_1)
  {
    DHT dht_a(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %f", dht_a.dht->id());
    DHT dht_b(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %f", dht_b.dht->id());
    dht_b.overlay->connect_recursive(*dht_a.overlay);
    DHT dht_c(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("third DHT: %f", dht_c.dht->id());
    dht_c.overlay->connect_recursive(*dht_a.overlay);
    auto b = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 3")
    {
      b->data(std::string("quorum_duel"));
      dht_a.dht->store(*b, infinit::model::STORE_INSERT);
    }
    BOOST_CHECK_EQUAL(dht_c.overlay->blocks().size(), 1u);
    ELLE_LOG("disconnect third DHT")
      dht_c.overlay->disconnect_all();
    ELLE_LOG("rebalance block to quorum of 1")
    {
      auto& local_a =
        dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
      local_a.rebalance(b->address(), {dht_a.dht->id()});
    }
    ELLE_LOG("reconnect third DHT")
      dht_c.overlay->connect_recursive(*dht_a.overlay);
    ELLE_LOG("write block to quorum of 1")
    {
      BOOST_CHECK_EQUAL(dht_c.overlay->blocks().size(), 1u);
      b->data(std::string("quorum_duel_edited"));
      dht_c.dht->store(*b, infinit::model::STORE_UPDATE);
    }
  }

  ELLE_TEST_SCHEDULED(quorum_duel_2)
  {
    DHT dht_a(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %f", dht_a.dht->id());
    DHT dht_b(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %f", dht_b.dht->id());
    dht_b.overlay->connect_recursive(*dht_a.overlay);
    DHT dht_c(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("third DHT: %f", dht_c.dht->id());
    dht_c.overlay->connect_recursive(*dht_a.overlay);
    auto b = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 3")
    {
      b->data(std::string("quorum_duel"));
      dht_a.dht->store(*b, infinit::model::STORE_INSERT);
    }
    BOOST_CHECK_EQUAL(dht_c.overlay->blocks().size(), 1u);
    ELLE_LOG("disconnect third DHT")
      dht_c.overlay->disconnect_all();
    ELLE_LOG("rebalance block to quorum of 2")
    {
      auto& local_a =
        dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
      local_a.rebalance(b->address(), {dht_a.dht->id(), dht_c.dht->id()});
    }
    ELLE_LOG("reconnect third DHT")
      dht_c.overlay->connect_recursive(*dht_a.overlay);
    ELLE_LOG("write block to quorum of 2")
    {
      BOOST_CHECK_EQUAL(dht_c.overlay->blocks().size(), 1u);
      b->data(std::string("quorum_duel_edited"));
      dht_c.dht->store(*b, infinit::model::STORE_UPDATE);
    }
  }

  class VersionHop:
    public infinit::model::ConflictResolver
  {
  public:
    VersionHop(blocks::Block& previous)
      : _previous(previous.data())
    {}

    virtual
    std::unique_ptr<blocks::Block>
    operator () (blocks::Block& failed,
                 blocks::Block& current,
                 infinit::model::StoreMode mode) override
    {
      BOOST_CHECK_EQUAL(current.data(), this->_previous);
      return failed.clone();
    }

    virtual
    void
    serialize(elle::serialization::Serializer& s,
              elle::Version const&) override
    {
      s.serialize("previous", this->_previous);
    }

    ELLE_ATTRIBUTE_R(elle::Buffer, previous);
  };

  class Local:
    public dht::consensus::Paxos::LocalPeer
  {
  public:
    typedef dht::consensus::Paxos::LocalPeer Super;
    typedef infinit::model::Address Address;

    template <typename ... Args>
    Local(Args&& ... args)
      : Super(std::forward<Args>(args)...)
      , _all_barrier()
      , _propose_barrier()
      , _propose_bypass(false)
      , _accept_barrier()
      , _accept_bypass(false)
      , _confirm_barrier()
      , _confirm_bypass(false)
    {
      this->_all_barrier.open();
      this->_propose_barrier.open();
      this->_accept_barrier.open();
      this->_confirm_barrier.open();
    }


    virtual
    boost::optional<PaxosClient::Accepted>
    propose(PaxosServer::Quorum peers,
            Address address,
            PaxosClient::Proposal const& p) override
    {
      this->_proposing(address);
      reactor::wait(this->all_barrier());
      if (!this->_propose_bypass)
        reactor::wait(this->propose_barrier());
      auto res = Super::propose(peers, address, p);
      this->_proposed(address);
      return res;
    }

    virtual
    PaxosClient::Proposal
    accept(PaxosServer::Quorum peers,
           Address address,
           PaxosClient::Proposal const& p,
           Value const& value) override
    {
      reactor::wait(this->all_barrier());
      if (!this->_accept_bypass)
        reactor::wait(this->accept_barrier());
      return Super::accept(peers, address, p, value);
    }

    virtual
    void
    confirm(PaxosServer::Quorum peers,
            Address address,
            PaxosClient::Proposal const& p) override
    {
      reactor::wait(this->all_barrier());
      if (!this->_confirm_bypass)
        reactor::wait(this->confirm_barrier());
      Super::confirm(peers, address, p);
    }

    virtual
    void
    _disappeared_schedule_eviction(infinit::model::Address id) override
    {
      ELLE_TRACE("%s: node %f disappeared, evict when signaled", this, id);
      this->_evict.connect([this, id] { this->_disappeared_evict(id); });
    }

    ELLE_ATTRIBUTE_RX(reactor::Barrier, all_barrier);
    ELLE_ATTRIBUTE_RX(reactor::Barrier, propose_barrier);
    ELLE_ATTRIBUTE_RW(bool, propose_bypass);
    ELLE_ATTRIBUTE_RX(boost::signals2::signal<void(Address)>, proposing);
    ELLE_ATTRIBUTE_RX(boost::signals2::signal<void(Address)>, proposed);
    ELLE_ATTRIBUTE_RX(reactor::Barrier, accept_barrier);
    ELLE_ATTRIBUTE_RW(bool, accept_bypass);
    ELLE_ATTRIBUTE_RX(reactor::Barrier, confirm_barrier);
    ELLE_ATTRIBUTE_RW(bool, confirm_bypass);
    ELLE_ATTRIBUTE_RX(boost::signals2::signal<void()>, evict);
  };

  static constexpr
  auto default_node_timeout = std::chrono::seconds(1);

  class InstrumentedPaxos:
    public dht::consensus::Paxos
  {
    typedef dht::consensus::Paxos Super;
    using Super::Super;
    std::unique_ptr<dht::Local>
    make_local(boost::optional<int> port,
               std::unique_ptr<infinit::storage::Storage> storage)
    {
      return elle::make_unique<Local>(
        *this,
        this->factor(),
        this->rebalance_auto_expand(),
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
          default_node_timeout),
        this->doughnut(),
        this->doughnut().id(),
        std::move(storage),
        port ? port.get() : 0);
    }
  };

  std::function<std::unique_ptr<dht::consensus::Consensus>(
    std::unique_ptr<dht::consensus::Consensus>)>
  instrument(int factor)
  {
    return [factor] (std::unique_ptr<dht::consensus::Consensus> c)
      -> std::unique_ptr<dht::consensus::Consensus>
    {
      return elle::make_unique<InstrumentedPaxos>(
        dht::consensus::doughnut = c->doughnut(),
        dht::consensus::replication_factor = factor);
    };
  }

  static
  std::unique_ptr<blocks::Block>
  make_block(DHT& client, bool immutable, std::string data_)
  {
    elle::Buffer data(std::move(data_));
    if (immutable)
      return client.dht->make_block<blocks::ImmutableBlock>(std::move(data));
    else
    {
      auto b = client.dht->make_block<blocks::MutableBlock>();
      b->data(std::move(data));
      return std::move(b);
    }
  }

  ELLE_TEST_SCHEDULED(expand_new_block, (bool, immutable))
  {
    DHT dht_a(make_consensus = instrument(2));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    DHT dht_b(make_consensus = instrument(2));
    dht_b.overlay->connect(*dht_a.overlay);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    DHT client(storage = nullptr);
    client.overlay->connect(*dht_a.overlay);
    auto b = make_block(client, immutable, "expand_new_block");
    ELLE_LOG("write block to one DHT")
      client.dht->store(*b, infinit::model::STORE_INSERT);
    auto op = infinit::overlay::OP_FETCH;
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 2, op)), 1u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 2, op)), 1u);
    ELLE_LOG("wait for rebalancing")
      reactor::wait(local_a.rebalanced(), b->address());
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 2, op)), 2u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 2, op)), 2u);
    ELLE_LOG("disconnect second DHT")
      dht_b.overlay->disconnect(*dht_a.overlay);
    ELLE_LOG("read block from second DHT")
      BOOST_CHECK_EQUAL(dht_b.dht->fetch(b->address())->data(), b->data());
  }

  ELLE_TEST_SCHEDULED(expand_newcomer, (bool, immutable))
  {
    DHT dht_a(make_consensus = instrument(3));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    DHT dht_b(make_consensus = instrument(3));
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    auto b = make_block(dht_a, immutable, "expand_newcomer");
    ELLE_LOG("write block to first DHT")
      dht_a.dht->store(*b, infinit::model::STORE_INSERT);
    // Block the new quorum election to check the balancing is done in
    // background.
    local_a.propose_barrier().close();
    // Wait until the first automatic expansion fails.
    reactor::wait(dht_a.overlay->looked_up(), b->address());
    ELLE_LOG("connect second DHT")
      dht_b.overlay->connect(*dht_a.overlay);
    static auto const op = infinit::overlay::OP_FETCH;
    if (!immutable)
    {
      reactor::wait(local_a.proposing(), b->address());
      BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 3, op)), 1u);
      BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 3, op)), 1u);
      // Insert another block, to check iterator invalidation while balancing.
      ELLE_LOG("write other block to first DHT")
      {
        local_a.propose_bypass(true);
        auto perturbate = dht_a.dht->make_block<blocks::MutableBlock>();
      perturbate->data(std::string("booh!"));
      dht_a.dht->store(*perturbate, infinit::model::STORE_INSERT);
      }
      local_a.propose_barrier().open();
    }
    ELLE_LOG("wait for rebalancing")
      reactor::wait(local_a.rebalanced(), b->address());
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 3, op)), 2u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 3, op)), 2u);
    if (!immutable)
    {
      auto& mb = dynamic_cast<blocks::MutableBlock&>(*b);
      ELLE_LOG("write block to both DHTs")
      {
        auto resolver = elle::make_unique<VersionHop>(mb);
        mb.data(std::string("expand'"));
        dht_b.dht->store(mb, infinit::model::STORE_UPDATE, std::move(resolver));
      }
    }
    ELLE_LOG("disconnect second DHT")
      dht_b.overlay->disconnect(*dht_a.overlay);
    ELLE_LOG("read block from second DHT")
      BOOST_CHECK_EQUAL(dht_b.dht->fetch(b->address())->data(), b->data());
  }

  ELLE_TEST_SCHEDULED(expand_concurrent)
  {
    DHT dht_a(make_consensus = instrument(3));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    DHT dht_b(make_consensus = instrument(3));
    auto& local_b = dynamic_cast<Local&>(*dht_b.dht->local());
    dht_b.overlay->connect(*dht_a.overlay);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    DHT dht_c(make_consensus = instrument(3));
    dht_c.overlay->connect(*dht_a.overlay);
    dht_c.overlay->connect(*dht_b.overlay);
    ELLE_LOG("third DHT: %s", dht_b.dht->id());
    DHT client(storage = nullptr);
    client.overlay->connect(*dht_a.overlay);
    client.overlay->connect(*dht_b.overlay);
    auto b = client.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to two DHT")
    {
      b->data(std::string("expand"));
      client.dht->store(*b, infinit::model::STORE_INSERT);
    }
    ELLE_LOG("wait for rebalancing")
    {
      boost::signals2::signal<void(infinit::model::Address)> rebalanced;
      boost::signals2::scoped_connection c_a =
        local_a.rebalanced().connect(rebalanced);
      boost::signals2::scoped_connection c_b =
        local_b.rebalanced().connect(rebalanced);
      reactor::wait(rebalanced, b->address());
    }
    auto op = infinit::overlay::OP_FETCH;
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 3, op)), 3u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 3, op)), 3u);
    BOOST_CHECK_EQUAL(size(dht_c.overlay->lookup(b->address(), 3, op)), 3u);
  }

  ELLE_TEST_SCHEDULED(expand_from_disk, (bool, immutable))
  {
    infinit::storage::Memory::Blocks storage_a;
    infinit::model::Address address;
    auto id_a = infinit::model::Address::random();
    ELLE_LOG("create block with 1 DHT")
    {
      DHT dht_a(id = id_a,
                make_consensus = instrument(3),
                storage = elle::make_unique<Memory>(storage_a));
      auto block = make_block(dht_a, immutable, "expand_from_disk");
      address = block->address();
      dht_a.dht->store(std::move(block), infinit::model::STORE_INSERT);
    }
    BOOST_CHECK_EQUAL(storage_a.size(), 1u);
    ELLE_LOG("restart with 2 DHTs")
    {
      DHT dht_a(id = id_a,
                make_consensus = instrument(3),
                storage = elle::make_unique<Memory>(storage_a));
      auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
      DHT dht_b(make_consensus = instrument(3));
      dht_b.overlay->connect(*dht_a.overlay);
      reactor::wait(local_a.rebalanced(), address);
    }
  }

  ELLE_TEST_SCHEDULED(rebalancing_while_destroyed)
  {
    DHT dht_a;
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    DHT dht_b;
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    auto b1 = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 1")
    {
      b1->data(std::string("extend_and_write 1"));
      dht_a.dht->store(*b1, infinit::model::STORE_INSERT);
    }
    dht_b.overlay->connect(*dht_a.overlay);
  }

  ELLE_TEST_SCHEDULED(evict_faulty, (bool, immutable))
  {
    DHT dht_a(make_consensus = instrument(3));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %f", dht_a.dht->id());
    DHT dht_b(make_consensus = instrument(3));
    auto& local_b = dynamic_cast<Local&>(*dht_b.dht->local());
    dht_b.overlay->connect(*dht_a.overlay);
    ELLE_LOG("second DHT: %f", dht_b.dht->id());
    DHT dht_c(make_consensus = instrument(3));
    dht_c.overlay->connect(*dht_a.overlay);
    dht_c.overlay->connect(*dht_b.overlay);
    ELLE_LOG("third DHT: %f", dht_c.dht->id());
    auto b = make_block(dht_a, immutable, "evict_faulty");
    ELLE_LOG("write block")
      dht_a.dht->store(*b, infinit::model::STORE_INSERT);
    DHT dht_d(make_consensus = instrument(3));
    dht_d.overlay->connect(*dht_a.overlay);
    dht_d.overlay->connect(*dht_b.overlay);
    dht_d.overlay->connect(*dht_c.overlay);
    ELLE_LOG("fourth DHT: %f", dht_d.dht->id());
    ELLE_LOG("disconnect third DHT")
    {
      dht_c.overlay->disconnect_all();
      local_a.evict()();
      local_b.evict()();
    }
    ELLE_LOG("wait for rebalancing")
    {
      boost::signals2::signal<void(infinit::model::Address)> rebalanced;
      boost::signals2::scoped_connection c_a =
        local_a.rebalanced().connect(rebalanced);
      boost::signals2::scoped_connection c_b =
        local_b.rebalanced().connect(rebalanced);
      reactor::wait(rebalanced, b->address());
    }
    ELLE_LOG("disconnect first DHT")
      dht_a.overlay->disconnect_all();
    ELLE_LOG("read block")
      BOOST_CHECK_EQUAL(dht_b.dht->fetch(b->address())->data(), b->data());
  }
}

// Since we use Locals, blocks dont go through serialization and thus
// are fetched already decoded
static void no_cheating(dht::Doughnut* d, std::unique_ptr<blocks::Block>& b)
{
  auto acb = dynamic_cast<dht::ACB*>(b.get());
  if (!acb)
    return;
  elle::Buffer buf;
  {
    elle::IOStream os(buf.ostreambuf());
    elle::serialization::binary::serialize(b, os);
  }
  elle::IOStream is(buf.istreambuf());
  elle::serialization::Context ctx;
  ctx.set(d);
  auto res =
    elle::serialization::binary::deserialize<std::unique_ptr<blocks::Block>>(
      is, true, ctx);
  b.reset(res.release());
}

ELLE_TEST_SCHEDULED(batch_quorum)
{
  auto owner_key = infinit::cryptography::rsa::keypair::generate(512);
  DHT dht_a(keys=owner_key, owner=owner_key);
  DHT dht_b(keys=owner_key, owner=owner_key);
  DHT dht_c(keys=owner_key, owner=owner_key);
  dht_b.overlay->connect(*dht_a.overlay);
  dht_c.overlay->connect(*dht_a.overlay);
  dht_b.overlay->connect(*dht_c.overlay);
  std::vector<infinit::model::Model::AddressVersion> addrs;
  for (int i=0; i<10; ++i)
  {
    auto block = dht_a.dht->make_block<blocks::ACLBlock>();
    block->data(std::string("foo"));
    addrs.push_back(std::make_pair(block->address(), boost::optional<int>()));
    const_cast<Overlay&>(dynamic_cast<Overlay const&>(*dht_a.overlay)).partial_addresses()[block->address()] = 1+(i%3);
    dht_b.dht->store(*block, infinit::model::STORE_INSERT);
  }
  int hit = 0;
  auto handler = [&](infinit::model::Address,
                     std::unique_ptr<blocks::Block> b,
                     std::exception_ptr ex)
    {
      if (ex)
      {
        try
        {
          std::rethrow_exception(ex);
        }
        catch (elle::Error const& e)
        {
          ELLE_ERR("boum %s", e);
        }
      }
      BOOST_CHECK(b);
      if (b)
        BOOST_CHECK_EQUAL(b->data(), std::string("foo"));
      if (b && !ex)
        ++hit;
    };
  dht_b.dht->fetch(addrs, handler);
  BOOST_CHECK_EQUAL(hit, 10);
  hit = 0;
  dht_a.dht->fetch(addrs, handler);
  BOOST_CHECK_EQUAL(hit, 10);
  dht_c.overlay->disconnect(*dht_a.overlay);
  dht_c.overlay->disconnect(*dht_b.overlay);
  hit = 0;
  dht_b.dht->fetch(addrs, handler);
  BOOST_CHECK_EQUAL(hit, 10);
  hit = 0;
  dht_a.dht->fetch(addrs, handler);
  BOOST_CHECK_EQUAL(hit, 10);
}

ELLE_TEST_SCHEDULED(admin_keys)
{
  auto owner_key = infinit::cryptography::rsa::keypair::generate(512);
  DHT dht(keys=owner_key, owner=owner_key);
  DHT client(storage = nullptr, keys=owner_key, owner=owner_key);
  client.overlay->connect(*dht.overlay);
  auto b0 = client.dht->make_block<blocks::ACLBlock>();
  b0->data(std::string("foo"));
  client.dht->store(*b0, infinit::model::STORE_INSERT);
  auto b1 = client.dht->make_block<blocks::ACLBlock>();
  b1->data(std::string("foo"));
  client.dht->store(*b1, infinit::model::STORE_INSERT);
  auto b2 = client.dht->fetch(b1->address());
  no_cheating(client.dht.get(), b2);
  BOOST_CHECK_EQUAL(b2->data().string(), "foo");
  // set server-side adm key but don't tell the client
  auto admin = infinit::cryptography::rsa::keypair::generate(512);
  dht.dht->admin_keys().r.push_back(admin.K());
  dynamic_cast<infinit::model::blocks::MutableBlock*>(b2.get())->data(std::string("bar"));
  BOOST_CHECK_THROW(client.dht->store(*b2, infinit::model::STORE_UPDATE), std::exception);
  auto b3 = client.dht->make_block<blocks::ACLBlock>();
  b3->data(std::string("baz"));
  BOOST_CHECK_THROW(client.dht->store(*b3, infinit::model::STORE_INSERT), std::exception);
  // tell the client
  client.dht->admin_keys().r.push_back(admin.K());
  b3 = client.dht->make_block<blocks::ACLBlock>();
  b3->data(std::string("baz"));
  BOOST_CHECK_NO_THROW(client.dht->store(*b3, infinit::model::STORE_INSERT));

  // check admin can actually read the block
  DHT cadm(storage = nullptr, keys=admin, owner=owner_key);
  cadm.dht->admin_keys().r.push_back(admin.K());
  cadm.overlay->connect(*dht.overlay);
  auto b4 = cadm.dht->fetch(b3->address());
  BOOST_CHECK_EQUAL(b4->data().string(), "baz");
  // but not the first one pushed before setting admin_key
  auto b0a = cadm.dht->fetch(b0->address());
  no_cheating(cadm.dht.get(), b0a);
  BOOST_CHECK_THROW(b0a->data(), std::exception);

  // do some stuff with blocks owned by admin
  auto ba = cadm.dht->make_block<blocks::ACLBlock>();
  ba->data(std::string("foo"));
  ba->set_permissions(*cadm.dht->make_user(elle::serialization::json::serialize(
    owner_key.K())), true, true);
  cadm.dht->store(*ba, infinit::model::STORE_INSERT);
  auto ba2 = cadm.dht->fetch(ba->address());
  no_cheating(cadm.dht.get(), ba2);
  BOOST_CHECK_EQUAL(ba2->data(), std::string("foo"));
  auto ba3 = client.dht->fetch(ba->address());
  no_cheating(client.dht.get(), ba3);
  BOOST_CHECK_EQUAL(ba3->data(), std::string("foo"));
  dynamic_cast<infinit::model::blocks::MutableBlock*>(ba3.get())->data(std::string("bar"));
  client.dht->store(*ba3, infinit::model::STORE_UPDATE);
  auto ba4 = cadm.dht->fetch(ba->address());
  no_cheating(cadm.dht.get(), ba4);
  BOOST_CHECK_EQUAL(ba4->data(), std::string("bar"));

  // check group admin key
  auto gadmin = infinit::cryptography::rsa::keypair::generate(512);
  DHT cadmg(storage = nullptr, keys=gadmin, owner=owner_key);
  cadmg.overlay->connect(*dht.overlay);
  {
    dht::Group g(*dht.dht, "g");
    g.create();
    g.add_member(*cadmg.dht->make_user(elle::serialization::json::serialize(
      gadmin.K())));
    cadmg.dht->admin_keys().group_r.push_back(g.public_control_key());
    dht.dht->admin_keys().group_r.push_back(g.public_control_key());
    client.dht->admin_keys().group_r.push_back(g.public_control_key());
  }

  auto bg = client.dht->make_block<blocks::ACLBlock>();
  bg->data(std::string("baz"));
  client.dht->store(*bg, infinit::model::STORE_INSERT);
  auto bg2 = cadmg.dht->fetch(bg->address());
  BOOST_CHECK_EQUAL(bg2->data(), std::string("baz"));
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
    auto _Name = boost::bind(Name, true);       \
    auto Name = _Name;                          \
    paxos->add(BOOST_TEST_CASE(Name));          \
  }                                             \
  {                                             \
    auto _Name = boost::bind(Name, false);      \
    auto Name = _Name;                          \
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
  {
    boost::unit_test::test_suite* remove_plain = BOOST_TEST_SUITE("removal");
    plain->add(remove_plain);
    boost::unit_test::test_suite* remove_paxos = BOOST_TEST_SUITE("removal");
    paxos->add(remove_paxos);
    auto* plain = remove_plain;
    auto* paxos = remove_paxos;
    using namespace removal;
    TEST(serialize_ACB_remove);
  }
#undef TEST
  paxos->add(BOOST_TEST_CASE(admin_keys));
  paxos->add(BOOST_TEST_CASE(batch_quorum));
  paxos->add(BOOST_TEST_CASE(wrong_quorum));
  {
    using namespace tests_paxos;
    paxos->add(BOOST_TEST_CASE(CHB_no_peer));
  }
  {
    boost::unit_test::test_suite* rebalancing = BOOST_TEST_SUITE("rebalancing");
    paxos->add(rebalancing);
    using namespace rebalancing;
    rebalancing->add(BOOST_TEST_CASE(extend_and_write), 0, valgrind(1));
    rebalancing->add(BOOST_TEST_CASE(shrink_and_write), 0, valgrind(1));
    rebalancing->add(BOOST_TEST_CASE(shrink_kill_and_write), 0, valgrind(1));
    rebalancing->add(BOOST_TEST_CASE(quorum_duel_1), 0, valgrind(1));
    rebalancing->add(BOOST_TEST_CASE(quorum_duel_2), 0, valgrind(1));
    {
      auto expand_new_CHB = [] () { expand_new_block(true); };
      auto expand_new_OKB = [] () { expand_new_block(false); };
      rebalancing->add(BOOST_TEST_CASE(expand_new_CHB), 0, valgrind(1));
      rebalancing->add(BOOST_TEST_CASE(expand_new_OKB), 0, valgrind(1));
    }
    {
      auto expand_newcomer_CHB = [] () { expand_newcomer(true); };
      auto expand_newcomer_OKB = [] () { expand_newcomer(false); };
      rebalancing->add(BOOST_TEST_CASE(expand_newcomer_CHB), 0, valgrind(1));
      rebalancing->add(BOOST_TEST_CASE(expand_newcomer_OKB), 0, valgrind(1));
    }
    rebalancing->add(BOOST_TEST_CASE(expand_concurrent), 0, valgrind(5));
    {
      auto expand_CHB_from_disk = [] () { expand_from_disk(true); };
      auto expand_OKB_from_disk = [] () { expand_from_disk(false); };
      rebalancing->add(BOOST_TEST_CASE(expand_CHB_from_disk), 0, valgrind(1));
      rebalancing->add(BOOST_TEST_CASE(expand_OKB_from_disk), 0, valgrind(1));
    }
    rebalancing->add(
      BOOST_TEST_CASE(rebalancing_while_destroyed), 0, valgrind(1));
    {
      auto evict_faulty_CHB = [] () { evict_faulty(true); };
      auto evict_faulty_OKB = [] () { evict_faulty(false); };
      rebalancing->add(BOOST_TEST_CASE(evict_faulty_CHB), 0, valgrind(1));
      rebalancing->add(BOOST_TEST_CASE(evict_faulty_OKB), 0, valgrind(1));
    }
  }
}
