#include <memory>

#include <boost/range/algorithm/count_if.hpp>
#include <boost/signals2/connection.hpp>

#include <elle/cast.hh>
#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/find.hh>
#include <elle/log.hh>
#include <elle/random.hh>
#include <elle/test.hh>
#include <elle/utils.hh>
#include <elle/Version.hh>

#ifndef ELLE_WINDOWS
# include <elle/reactor/network/unix-domain-socket.hh>
#endif

#include <memo/model/Conflict.hh>
#include <memo/model/MissingBlock.hh>
#include <memo/model/MonitoringServer.hh>
#include <memo/model/blocks/ACLBlock.hh>
#include <memo/model/blocks/ImmutableBlock.hh>
#include <memo/model/blocks/MutableBlock.hh>
#include <memo/model/doughnut/ACB.hh>
#include <memo/model/doughnut/Cache.hh>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/Group.hh>
#include <memo/model/doughnut/Local.hh>
#include <memo/model/doughnut/NB.hh>
#include <memo/model/doughnut/Remote.hh>
#include <memo/model/doughnut/UB.hh>
#include <memo/model/doughnut/User.hh>
#include <memo/model/doughnut/ValidationFailed.hh>
#include <memo/model/doughnut/consensus/Paxos.hh>
#include <memo/overlay/Stonehenge.hh>
#include <memo/silo/Memory.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("test.doughnut");

using namespace std::literals;

namespace blocks = memo::model::blocks;
namespace dht = memo::model::doughnut;
using namespace memo::silo;

using dht::consensus::Paxos;

ELLE_DAS_SYMBOL(keys_a);
ELLE_DAS_SYMBOL(keys_b);
ELLE_DAS_SYMBOL(keys_c);
ELLE_DAS_SYMBOL(id_a);
ELLE_DAS_SYMBOL(id_b);
ELLE_DAS_SYMBOL(id_c);
ELLE_DAS_SYMBOL(storage_a);
ELLE_DAS_SYMBOL(storage_b);
ELLE_DAS_SYMBOL(storage_c);
ELLE_DAS_SYMBOL(version_a);
ELLE_DAS_SYMBOL(version_b);
ELLE_DAS_SYMBOL(version_c);
ELLE_DAS_SYMBOL(monitoring_socket_path_a);
ELLE_DAS_SYMBOL(encrypt_options);

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
    elle::das::named::prototype(
      paxos = true,
      ::keys_a = elle::cryptography::rsa::keypair::generate(key_size()),
      ::keys_b = elle::cryptography::rsa::keypair::generate(key_size()),
      ::keys_c = elle::cryptography::rsa::keypair::generate(key_size()),
      id_a = special_id(10),
      id_b = special_id(11),
      id_c = special_id(12),
      storage_a = nullptr,
      storage_b = nullptr,
      storage_c = nullptr,
      version_a = boost::optional<elle::Version>(),
      version_b = boost::optional<elle::Version>(),
      version_c = boost::optional<elle::Version>(),
      monitoring_socket_path_a = boost::optional<boost::filesystem::path>(),
      encrypt_options = memo::model::doughnut::EncryptOptions(),
      make_overlay =
      [] (int,
          memo::model::NodeLocations peers,
          std::shared_ptr<memo::model::doughnut::Local> local,
          memo::model::doughnut::Doughnut& d)
      {
        return std::make_unique<memo::overlay::Stonehenge>(
          peers, std::move(local), &d);
      },
      dht::consensus_builder = dht::Doughnut::ConsensusBuilder(),
      with_cache = false
      ).call([this] (
        bool paxos,
        elle::cryptography::rsa::KeyPair keys_a,
        elle::cryptography::rsa::KeyPair keys_b,
        elle::cryptography::rsa::KeyPair keys_c,
        memo::model::Address id_a,
        memo::model::Address id_b,
        memo::model::Address id_c,
        std::unique_ptr<Silo> storage_a,
        std::unique_ptr<Silo> storage_b,
        std::unique_ptr<Silo> storage_c,
        boost::optional<elle::Version> version_a,
        boost::optional<elle::Version> version_b,
        boost::optional<elle::Version> version_c,
        boost::optional<boost::filesystem::path> monitoring_socket_path_a,
        memo::model::doughnut::EncryptOptions encrypt_options,
        std::function<
          std::unique_ptr<memo::overlay::Stonehenge>(
            int,
            memo::model::NodeLocations peers,
            std::shared_ptr<
              memo::model::doughnut::Local> local,
            memo::model::doughnut::Doughnut& d)> make_overlay,
        dht::Doughnut::ConsensusBuilder consensus_builder,
        bool cache)
              {
                this->init(paxos,
                           std::move(keys_a),
                           std::move(keys_b),
                           std::move(keys_c),
                           id_a, id_b, id_c,
                           std::move(storage_a),
                           std::move(storage_b),
                           std::move(storage_c) ,
                           version_a, version_b, version_c,
                           std::move(monitoring_socket_path_a),
                           std::move(encrypt_options),
                           std::move(make_overlay),
                           std::move(consensus_builder),
                           cache);
              }, std::forward<Args>(args)...);
  }

  std::shared_ptr<elle::cryptography::rsa::KeyPair> keys_a;
  std::shared_ptr<elle::cryptography::rsa::KeyPair> keys_b;
  std::shared_ptr<elle::cryptography::rsa::KeyPair> keys_c;
  std::shared_ptr<dht::Doughnut> dht_a;
  std::shared_ptr<dht::Doughnut> dht_b;
  std::shared_ptr<dht::Doughnut> dht_c;

private:
  void
  init(bool paxos,
       elle::cryptography::rsa::KeyPair keys_a,
       elle::cryptography::rsa::KeyPair keys_b,
       elle::cryptography::rsa::KeyPair keys_c,
       memo::model::Address id_a,
       memo::model::Address id_b,
       memo::model::Address id_c,
       std::unique_ptr<Silo> storage_a,
       std::unique_ptr<Silo> storage_b,
       std::unique_ptr<Silo> storage_c,
       boost::optional<elle::Version> version_a,
       boost::optional<elle::Version> version_b,
       boost::optional<elle::Version> version_c,
       boost::optional<boost::filesystem::path> monitoring_socket_path_a,
       memo::model::doughnut::EncryptOptions encrypt_options,
       std::function<
         std::unique_ptr<memo::overlay::Stonehenge>(
           int,
           memo::model::NodeLocations peers,
           std::shared_ptr<memo::model::doughnut::Local> local,
           memo::model::doughnut::Doughnut& d)> make_overlay,
       dht::Doughnut::ConsensusBuilder consensus_builder,
       bool cache)
  {
    if (!consensus_builder)
      if (paxos)
        consensus_builder = [&] (dht::Doughnut& dht)
          {
            return std::make_unique<dht::consensus::Paxos>(dht, 3);
          };
      else
        consensus_builder = [&] (dht::Doughnut& dht)
          {
            return std::make_unique<dht::consensus::Consensus>(dht);
          };
    if (cache)
      consensus_builder = [consensus_builder] (dht::Doughnut& dht)
        {
          return std::make_unique<dht::consensus::Cache>(
            consensus_builder(dht));
        };
    auto const members = memo::model::NodeLocations
      {
        {id_a, memo::model::Endpoints()},
        {id_b, memo::model::Endpoints()},
        {id_c, memo::model::Endpoints()},
      };
    std::vector<memo::overlay::Stonehenge*> stonehenges;
    make_overlay =
      [make_overlay, &stonehenges] (
        int n,
        memo::model::NodeLocations peers,
        std::shared_ptr<memo::model::doughnut::Local> local,
        memo::model::doughnut::Doughnut& d)
      {
        auto res = make_overlay(
          n, std::move(peers), std::move(local), d);
        stonehenges.emplace_back(res.get());
        return res;
      };
    // dht_a.
    {
      this->keys_a =
        std::make_shared<elle::cryptography::rsa::KeyPair>(std::move(keys_a));
      if (!storage_a)
        storage_a = std::make_unique<Memory>();
      auto const passport_a = dht::Passport{
        this->keys_a->K(), "network-name", *this->keys_a};
      this->dht_a = std::make_shared<dht::Doughnut>(
        id_a,
        this->keys_a,
        this->keys_a->public_key(),
        passport_a,
        consensus_builder,
        [=] (memo::model::doughnut::Doughnut& d,
             std::shared_ptr<memo::model::doughnut::Local> local)
        {
          return make_overlay(0, members, std::move(local), d);
        },
        boost::optional<int>(),
        boost::optional<boost::asio::ip::address>(),
        std::move(storage_a),
        dht::version = version_a,
        memo::model::doughnut::monitoring_socket_path =
          monitoring_socket_path_a,
        memo::model::doughnut::encrypt_options = encrypt_options);
    }
    // dht_b.
    {
      this->keys_b =
        std::make_shared<elle::cryptography::rsa::KeyPair>(std::move(keys_b));
      if (!storage_b)
        storage_b = std::make_unique<Memory>();
      auto const passport_b = dht::Passport{
        this->keys_b->K(), "network-name", *this->keys_a};
      this->dht_b = std::make_shared<dht::Doughnut>(
        id_b,
        this->keys_b,
        this->keys_a->public_key(),
        passport_b,
        consensus_builder,
        [=] (memo::model::doughnut::Doughnut& d,
             std::shared_ptr<memo::model::doughnut::Local> local)
        {
          return make_overlay(1, members, std::move(local), d);
        },
        boost::optional<int>(),
        boost::optional<boost::asio::ip::address>(),
        std::move(storage_b),
        dht::version = version_b,
        memo::model::doughnut::encrypt_options = encrypt_options);
    }
    // dht_c.
    {
      this->keys_c =
        std::make_shared<elle::cryptography::rsa::KeyPair>(std::move(keys_c));
      if (!storage_c)
        storage_c = std::make_unique<Memory>();
      auto const passport_c = dht::Passport{
        this->keys_c->K(), "network-name", *this->keys_a};
      this->dht_c = std::make_shared<dht::Doughnut>(
        id_c,
        this->keys_c,
        this->keys_a->public_key(),
        passport_c,
        consensus_builder,
        [=] (memo::model::doughnut::Doughnut& d,
             std::shared_ptr<memo::model::doughnut::Local> local)
        {
          return make_overlay(2, members, std::move(local), d);
        },
        boost::optional<int>(),
        boost::optional<boost::asio::ip::address>(),
        std::move(storage_c),
        dht::version = version_c,
        memo::model::doughnut::encrypt_options = encrypt_options);
    }
    for (auto* stonehenge: stonehenges)
      for (auto& peer: stonehenge->peers())
      {
        auto const dht = [this, &peer, &id_a, &id_b, &id_c]
          {
            if (peer.id() == id_a)      return this->dht_a;
            else if (peer.id() == id_b) return this->dht_b;
            else if (peer.id() == id_c) return this->dht_c;
            else ELLE_ABORT("unknown doughnut id: %f", peer.id());
          }();
        elle::unconst(peer.endpoints()).emplace(
          boost::asio::ip::address::from_string("127.0.0.1"),
          dht->local()->server_endpoint().port());
      }
  }
};

template<typename C>
int
mutable_block_count(C const& c)
{
  return boost::count_if(c,
                         [](auto const& i)
                         {
                           return i.mutable_block();
                         });
}

ELLE_TEST_SCHEDULED(CHB, (bool, paxos))
{
  auto dhts = DHTs(paxos);
  auto& dht = *dhts.dht_a;
  {
    auto data = elle::Buffer("\\_o<");
    auto block = dht.make_block<blocks::ImmutableBlock>(data);
    auto addr = block->address();
    ELLE_LOG("store block")
      dht.seal_and_insert(*block);
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
    auto data = elle::Buffer("\\_o<");
    block->data(elle::Buffer(data));
    auto addr = block->address();
    ELLE_LOG("store mutable block")
      dht.seal_and_insert(*block);
    auto updated = elle::Buffer(">o_/");
    block->data(elle::Buffer(updated));
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(addr)->data(), data);
    ELLE_LOG("store updated mutable block")
      dht.seal_and_update(*block);
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(addr)->data(), updated);
    ELLE_LOG("remove block")
      dht.remove(addr);
  }
}

ELLE_TEST_SCHEDULED(missing_block, (bool, paxos))
{
  using namespace memo::model;
  DHTs dhts(paxos);
  ELLE_LOG("fetch immutable block")
    BOOST_CHECK_THROW(dhts.dht_a->fetch(
                        Address::random(flags::immutable_block)),
                      MissingBlock);
  ELLE_LOG("fetch mutable block")
    BOOST_CHECK_THROW(dhts.dht_a->fetch(
                        Address::random(flags::mutable_block)),
                      MissingBlock);
}

ELLE_TEST_SCHEDULED(async, (bool, paxos))
{
  DHTs dhts(paxos);
  auto& dht = *dhts.dht_c;
  {
    auto data = elle::Buffer("\\_o<");
    auto block = dht.make_block<blocks::ImmutableBlock>(data);
    std::vector<std::unique_ptr<blocks::ImmutableBlock>> blocks_;
    for (int i = 0; i < 10; ++i)
    {
      auto s = elle::sprintf("\\_o< %d", i);
      auto data = elle::Buffer(elle::sprintf(s).c_str(),
                        (int)std::strlen(s.c_str()));
      blocks_.push_back(dht.make_block<blocks::ImmutableBlock>(data));
    }
    ELLE_LOG("store block")
      dht.seal_and_insert(*block);
    for (auto& block: blocks_)
      dht.seal_and_insert(*block);
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
    for (auto& block: blocks_)
      dht.fetch(block->address());
    ELLE_LOG("remove block")
      dht.remove(block->address());
  }
  {
    auto block = dht.make_block<blocks::MutableBlock>();
    auto data = elle::Buffer("\\_o<");
    block->data(elle::Buffer(data));
    ELLE_LOG("store block")
      dht.seal_and_insert(*block);
    auto updated = elle::Buffer(">o_/");
    block->data(elle::Buffer(updated));
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
    ELLE_LOG("store block")
      dht.seal_and_update(*block);
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
  auto data = elle::Buffer("\\_o<");
  block->data(elle::Buffer(data));
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->seal_and_insert(*block);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_THROW(fetched->data(), elle::Error);
    auto acb = elle::cast<blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-("));
    ELLE_LOG("other: stored edited ACB")
      BOOST_CHECK_THROW(dhts.dht_b->seal_and_update(*acb),
                        dht::ValidationFailed);
  }
  ELLE_LOG("owner: add ACB read permissions")
    block->set_permissions(dht::User(dhts.keys_b->K(), ""), true, false);
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->seal_and_update(*block);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_EQUAL(fetched->data(), "\\_o<");
    auto acb = elle::cast<blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-("));
    ELLE_LOG("other: stored edited ACB")
      BOOST_CHECK_THROW(dhts.dht_b->seal_and_update(*acb),
                        dht::ValidationFailed);
  }
  ELLE_LOG("owner: add ACB write permissions")
    block->set_permissions(dht::User(dhts.keys_b->K(), ""), true, true);
  ELLE_LOG("owner: store ACB")
    dhts.dht_a->seal_and_update(*block);
  {
    ELLE_LOG("other: fetch ACB");
    auto fetched = dhts.dht_b->fetch(block->address());
    BOOST_CHECK_EQUAL(fetched->data(), "\\_o<");
    auto acb = elle::cast<blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-)"));
    ELLE_LOG("other: stored edited ACB")
      dhts.dht_b->seal_and_update(*acb);
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
  auto block = std::make_unique<dht::NB>(
    *dhts.dht_a, "blockname", elle::Buffer("blockdata"));
  ELLE_LOG("owner: store NB")
    dhts.dht_a->seal_and_insert(*block);
  {
    ELLE_LOG("other: fetch NB");
    auto fetched = dhts.dht_b->fetch(
      dht::NB::address(dhts.keys_a->K(), "blockname", dhts.dht_b->version()));
    BOOST_CHECK_EQUAL(fetched->data(), "blockdata");
    auto nb = elle::cast<dht::NB>::runtime(fetched);
    BOOST_CHECK(nb);
  }
  { // overwrite
    auto block = std::make_unique<dht::NB>(
      *dhts.dht_a, "blockname", elle::Buffer("blockdatb"));
    BOOST_CHECK_THROW(dhts.dht_a->seal_and_update(*block), std::exception);
  }
  // remove and remove protection
  BOOST_CHECK_THROW(
    dhts.dht_a->remove(dht::NB::address(dhts.keys_a->K(), "blockname",
                                        dhts.dht_a->version()),
                       memo::model::blocks::RemoveSignature()),
    std::exception);
  BOOST_CHECK_THROW(
    dhts.dht_b->remove(dht::NB::address(dhts.keys_a->K(), "blockname",
                                        dhts.dht_b->version())),
    std::exception);
  dhts.dht_a->remove(dht::NB::address(dhts.keys_a->K(), "blockname",
                                      dhts.dht_a->version()));
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
    dhta->seal_and_insert(uba);
    dhta->seal_and_insert(ubarev);
  }
  auto ruba = dhta->fetch(dht::UB::hash_address(dhta->keys().K(), *dhta));
  BOOST_CHECK(ruba);
  auto* uba = dynamic_cast<dht::UB*>(ruba.get());
  BOOST_CHECK(uba);
  dht::UB ubf(dhta.get(), "duck", dhta->keys().K(), true);
  ELLE_LOG("fail storing different UB")
  {
    BOOST_CHECK_THROW(dhta->seal_and_insert(ubf), std::exception);
    BOOST_CHECK_THROW(dhtb->seal_and_insert(ubf), std::exception);
  }
  ELLE_LOG("fail removing RUB")
  {
    BOOST_CHECK_THROW(dhtb->remove(ruba->address()), std::exception);
    BOOST_CHECK_THROW(
      dhtb->remove(ruba->address(), memo::model::blocks::RemoveSignature()),
      std::exception);
    BOOST_CHECK_THROW(
      dhta->remove(ruba->address(), memo::model::blocks::RemoveSignature()),
      std::exception);
  }
  ELLE_LOG("remove RUB")
    dhta->remove(ruba->address());
  ELLE_LOG("store different UB")
    dhtb->seal_and_insert(ubf);
}

namespace removal
{
  ELLE_TEST_SCHEDULED(serialize_ACB_remove, (bool, paxos))
  {
    Memory::Blocks dht_storage;
    auto dht_id = memo::model::Address::random();
    memo::model::Address address;
    // Store signature removal in the first run so the second run of the DHT
    // does not fetch the block before removing it. This tests the block is
    // still reloaded without a previous fetch.
    elle::Buffer rs_bad;
    elle::Buffer rs_good;
    ELLE_LOG("store block")
    {
      auto dht = DHT(id = dht_id,
              storage = std::make_unique<Memory>(dht_storage));
      auto b = dht.dht->make_block<blocks::ACLBlock>();
      address = b->address();
      b->data(std::string("removal/serialize_ACB_remove"));
      dht.dht->seal_and_insert(*b);
      rs_bad =
        elle::serialization::binary::serialize(b->sign_remove(*dht.dht));
      b->data([] (elle::Buffer& b) { b.append("_", 1); });
      dht.dht->seal_and_update(*b);
      rs_good =
        elle::serialization::binary::serialize(b->sign_remove(*dht.dht));
    }
    ELLE_LOG("fail removing block")
    {
      auto dht = DHT(id = dht_id,
              storage = std::make_unique<Memory>(dht_storage));
      elle::serialization::Context ctx;
      ctx.set<memo::model::doughnut::Doughnut*>(dht.dht.get());
      auto sig = elle::serialization::binary::deserialize<
        blocks::RemoveSignature>(rs_bad, true, ctx);
      // we send a remove with an obsolete signature, so consensus will retry,
      // but will fail to call sign_remove() since we don't have the proper keys
      BOOST_CHECK_THROW(dht.dht->remove(address, sig),
                        memo::model::doughnut::ValidationFailed);
    }
    ELLE_LOG("remove block")
    {
      auto dht = DHT(id = dht_id,
              storage = std::make_unique<Memory>(dht_storage));
      elle::serialization::Context ctx;
      ctx.set<memo::model::doughnut::Doughnut*>(dht.dht.get());
      auto sig = elle::serialization::binary::deserialize<
        blocks::RemoveSignature>(rs_good, true, ctx);
      dht.dht->remove(address, sig);
    }
    BOOST_CHECK(!contains(dht_storage, address));
  }
}

class AppendConflictResolver
  : public memo::model::ConflictResolver
{
  std::unique_ptr<blocks::Block>
  operator () (blocks::Block& old,
               blocks::Block& current) override
  {
    auto res = std::dynamic_pointer_cast<blocks::MutableBlock>(current.clone());
    res->data([] (elle::Buffer& data) { data.append("B", 1); });
    return std::unique_ptr<blocks::Block>(res.release());
  }

  std::string
  description() const override
  {
    return "Append data to block";
  }

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
    block_alice->data(elle::Buffer("A"));
    block_alice->set_permissions(
      dht::User(dhts.keys_b->K(), "bob"), true, true);
  }
  ELLE_LOG("alice: store block")
    dhts.dht_a->seal_and_insert(*block_alice);
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
    block_alice->data(elle::Buffer("AA"));
    dhts.dht_a->seal_and_update(*block_alice);
  }
  ELLE_LOG("bob: modify block")
  {
    block_bob->data(elle::Buffer("AB"));
    BOOST_CHECK_THROW(
      dhts.dht_b->seal_and_update(*block_bob),
      memo::model::Conflict);
    dhts.dht_b->seal_and_update(*block_bob,
                                std::make_unique<AppendConflictResolver>());
  }
  ELLE_LOG("alice: fetch block")
  {
    BOOST_CHECK_EQUAL(
      dhts.dht_a->fetch(block_alice->address())->data(), "AAB");
  }
}

void
noop(Silo*)
{}

ELLE_TEST_SCHEDULED(restart, (bool, paxos))
{
  auto keys_a = elle::cryptography::rsa::keypair::generate(key_size());
  auto keys_b = elle::cryptography::rsa::keypair::generate(key_size());
  auto keys_c = elle::cryptography::rsa::keypair::generate(key_size());
  auto id_a = memo::model::Address::random(0); // FIXME
  auto id_b = memo::model::Address::random(0); // FIXME
  auto id_c = memo::model::Address::random(0); // FIXME
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
      std::make_unique<Memory>(storage_a),
      std::make_unique<Memory>(storage_b),
      std::make_unique<Memory>(storage_c)
      );
    // iblock =
    //   dhts.dht_a->make_block<blocks::ImmutableBlock>(
    //     elle::Buffer("immutable", 9));
    // dhts.dht_a->store(*iblock);
    mblock =
      dhts.dht_a->make_block<blocks::MutableBlock>(
        elle::Buffer("mutable"));
    dhts.dht_a->seal_and_insert(*mblock);
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
      std::make_unique<Memory>(storage_a),
      std::make_unique<Memory>(storage_b),
      std::make_unique<Memory>(storage_c)
      );
    // auto ifetched = dhts.dht_a->fetch(iblock->address());
    // BOOST_CHECK_EQUAL(iblock->data(), ifetched->data());
    auto mfetched = dhts.dht_a->fetch(mblock->address());
    BOOST_CHECK_EQUAL(mblock->data(), mfetched->data());
  }
}

namespace tests_paxos
{
  /// Make one of the overlay return a partial quorum, missing one of the three
  /// members, and check it gets fixed.
  class WrongQuorumStonehenge
    : public memo::overlay::Stonehenge
  {
  public:
    template <typename ... Args>
    WrongQuorumStonehenge(Args&& ... args)
      : memo::overlay::Stonehenge(std::forward<Args>(args)...)
      , fail(false)
    {}

    elle::reactor::Generator<memo::overlay::Overlay::WeakMember>
    _lookup(memo::model::Address address, int n, bool fast) const override
    {
      return memo::overlay::Stonehenge::_lookup(address,
                                                   fail ? n - 1 : n,
                                                   fast);
    }

    bool fail;
  };

  ELLE_TEST_SCHEDULED(wrong_quorum)
  {
    auto stonehenge = static_cast<WrongQuorumStonehenge*>(nullptr);
    auto dhts = DHTs(
      make_overlay =
      [&stonehenge] (int dht,
                     memo::model::NodeLocations peers,
                     std::shared_ptr<memo::model::doughnut::Local> local,
                     memo::model::doughnut::Doughnut& d)
      {
        if (dht == 0)
        {
          stonehenge = new WrongQuorumStonehenge(peers, std::move(local), &d);
          return std::unique_ptr<memo::overlay::Stonehenge>(stonehenge);
        }
        else
          return std::make_unique<memo::overlay::Stonehenge>(
            peers, std::move(local), &d);
      });
    auto block = dhts.dht_a->make_block<blocks::MutableBlock>();
    {
      auto data = elle::Buffer("\\_o<");
      block->data(elle::Buffer(data));
      ELLE_LOG("store block")
        dhts.dht_a->seal_and_insert(*block);
      auto updated = elle::Buffer(">o_/");
      block->data(elle::Buffer(updated));
      stonehenge->fail = true;
      ELLE_LOG("store updated block")
        dhts.dht_a->seal_and_update(*block);
    }
  }

  ELLE_TEST_SCHEDULED(batch_quorum)
  {
    auto owner_key = elle::cryptography::rsa::keypair::generate(512);
    auto dht_a = DHT(keys=owner_key, owner=owner_key);
    auto dht_b = DHT(keys=owner_key, owner=owner_key);
    auto dht_c = DHT(keys=owner_key, owner=owner_key);
    dht_b.overlay->connect(*dht_a.overlay);
    dht_c.overlay->connect(*dht_a.overlay);
    dht_b.overlay->connect(*dht_c.overlay);
    std::vector<memo::model::Model::AddressVersion> addrs;
    for (int i=0; i<10; ++i)
    {
      auto block = dht_a.dht->make_block<blocks::ACLBlock>();
      block->data(std::string("foo"));
      addrs.push_back(std::make_pair(block->address(), boost::optional<int>()));
      const_cast<Overlay&>(dynamic_cast<Overlay const&>(*dht_a.overlay)).
        partial_addresses()[block->address()] = 1 + (i % 3);
      dht_b.dht->seal_and_insert(*block);
    }
    int hit = 0;
    auto handler = [&](memo::model::Address,
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
    dht_b.dht->multifetch(addrs, handler);
    BOOST_CHECK_EQUAL(hit, 10);
    hit = 0;
    dht_a.dht->multifetch(addrs, handler);
    BOOST_CHECK_EQUAL(hit, 10);
    dht_c.overlay->disconnect(*dht_a.overlay);
    dht_c.overlay->disconnect(*dht_b.overlay);
    hit = 0;
    dht_b.dht->multifetch(addrs, handler);
    BOOST_CHECK_EQUAL(hit, 10);
    hit = 0;
    dht_a.dht->multifetch(addrs, handler);
    BOOST_CHECK_EQUAL(hit, 10);
  }

  ELLE_TEST_SCHEDULED(CHB_no_peer)
  {
    auto dht = DHT(storage = nullptr);
    auto chb = dht.dht->make_block<blocks::ImmutableBlock>();
    BOOST_CHECK_THROW(dht.dht->seal_and_insert(*chb),
                      elle::Error);
  }
}

ELLE_TEST_SCHEDULED(cache, (bool, paxos))
{
  DHTs dhts(paxos, with_cache = true);
  auto cache =
    dynamic_cast<dht::consensus::Cache*>(dhts.dht_a->consensus().get());
  BOOST_REQUIRE(cache);
  // Check a null block is never stored in cache.
  {
    auto block = dhts.dht_a->make_block<blocks::MutableBlock>();
    auto data = elle::Buffer("cached");
    block->data(elle::Buffer(data));
    auto addr = block->address();
    ELLE_LOG("store block")
      dhts.dht_a->seal_and_insert(*block);
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
cycle(memo::model::doughnut::Doughnut& dht,
      std::unique_ptr<blocks::Block> b)
{
  elle::Buffer buf;
  {
    elle::IOStream os(buf.ostreambuf());
    elle::serialization::binary::SerializerOut sout(os, false);
    sout.set_context(memo::model::doughnut::ACBDontWaitForSignature{});
    sout.set_context(memo::model::doughnut::OKBDontWaitForSignature{});
    sout.serialize_forward(b);
  }
  elle::IOStream is(buf.istreambuf());
  elle::serialization::binary::SerializerIn sin(is, false);
  sin.set_context<memo::model::Model*>(&dht); // FIXME: needed ?
  sin.set_context<memo::model::doughnut::Doughnut*>(&dht);
  sin.set_context(memo::model::doughnut::ACBDontWaitForSignature{});
  sin.set_context(memo::model::doughnut::OKBDontWaitForSignature{});
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
    dhts.dht_a->insert(std::move(cb));
    cb = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(cb->data(), elle::Buffer("foo"));
  }
  { // wait for signature
    auto b =  dhts.dht_a->make_block<blocks::ACLBlock>();
    b->data(elle::Buffer("foo"));
    b->seal();
    elle::reactor::sleep(100ms);
    auto addr = b->address();
    auto cb = cycle(*dhts.dht_a, std::move(b));
    dhts.dht_a->insert(std::move(cb));
    cb = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(cb->data(), elle::Buffer("foo"));
  }
  { // block we dont own
    auto block_alice = dhts.dht_a->make_block<blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1"));
    block_alice->set_permissions(dht::User(dhts.keys_b->K(), "bob"),
                                 true, true);
    auto addr = block_alice->address();
    dhts.dht_a->insert(std::move(block_alice));
    auto block_bob = dhts.dht_b->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("alice_1"));
    dynamic_cast<blocks::MutableBlock*>(block_bob.get())->data(
      elle::Buffer("bob_1"));
    block_bob->seal();
    block_bob = cycle(*dhts.dht_b, std::move(block_bob));
    dhts.dht_b->update(std::move(block_bob));
    block_bob = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("bob_1"));
  }
  { // signing with group key
    std::unique_ptr<elle::cryptography::rsa::PublicKey> gkey;
    {
      memo::model::doughnut::Group g(*dhts.dht_a, "g");
      g.create();
      g.add_member(dht::User(dhts.keys_b->K(), "bob"));
      gkey.reset(new elle::cryptography::rsa::PublicKey(
                   g.public_control_key()));
    }
    auto block_alice = dhts.dht_a->make_block<blocks::ACLBlock>();
    block_alice->data(elle::Buffer("alice_1"));
    block_alice->set_permissions(dht::User(*gkey, "@g"), true, true);
    auto addr = block_alice->address();
    dhts.dht_a->insert(std::move(block_alice));
    auto block_bob = dhts.dht_b->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("alice_1"));
    dynamic_cast<blocks::MutableBlock*>(block_bob.get())->data(
      elle::Buffer("bob_1"));
    block_bob->seal();
    block_bob = cycle(*dhts.dht_b, std::move(block_bob));
    dhts.dht_b->update(std::move(block_bob));
    block_bob = dhts.dht_a->fetch(addr);
    BOOST_CHECK_EQUAL(block_bob->data(), elle::Buffer("bob_1"));
  }
}

#ifndef ELLE_WINDOWS
ELLE_TEST_SCHEDULED(monitoring, (bool, paxos))
{
  auto keys_a = elle::cryptography::rsa::keypair::generate(key_size());
  auto keys_b = elle::cryptography::rsa::keypair::generate(key_size());
  auto keys_c = elle::cryptography::rsa::keypair::generate(key_size());
  auto id_a = memo::model::Address::random(0); // FIXME
  auto id_b = memo::model::Address::random(0); // FIXME
  auto id_c = memo::model::Address::random(0); // FIXME
  Memory::Blocks storage_a;
  Memory::Blocks storage_b;
  Memory::Blocks storage_c;
  elle::filesystem::TemporaryDirectory d;
  auto monitoring_path = d.path() / "monitoring.sock";
  DHTs dhts(
    paxos,
    keys_a,
    keys_b,
    keys_c,
    id_a,
    id_b,
    id_c,
    std::make_unique<Memory>(storage_a),
    std::make_unique<Memory>(storage_b),
    std::make_unique<Memory>(storage_c),
    monitoring_socket_path_a = monitoring_path
  );
  BOOST_CHECK(boost::filesystem::exists(monitoring_path));
  elle::reactor::network::UnixDomainSocket socket(monitoring_path);
  using Monitoring = memo::model::MonitoringServer;
  using Query = memo::model::MonitoringServer::MonitorQuery::Query;
  auto do_query = [&] (Query query_val) -> elle::json::Object
    {
      auto query = Monitoring::MonitorQuery(query_val);
      elle::serialization::json::serialize(query, socket, false, false);
      return boost::any_cast<elle::json::Object>(elle::json::read(socket));
    };
  {
    Monitoring::MonitorResponse res(do_query(Query::Status));
    BOOST_CHECK(res.success);
  }
  {
    Monitoring::MonitorResponse res(do_query(Query::Stats));
    auto obj = res.result.get();
    BOOST_CHECK_EQUAL(obj.count("consensus"), 1);
    BOOST_CHECK_EQUAL(obj.count("overlay"), 1);
    // UTP was temporarily deprecated.
    // BOOST_CHECK_EQUAL(boost::any_cast<std::string>(obj["protocol"]), "all");
    BOOST_TEST(boost::any_cast<std::string>(obj["protocol"]) == "tcp");
    BOOST_CHECK_EQUAL(
      boost::any_cast<elle::json::Array>(obj["peers"]).size(), 3);
    auto redundancy = boost::any_cast<elle::json::Object>(obj["redundancy"]);
    if (paxos)
    {
      BOOST_CHECK_EQUAL(boost::any_cast<int64_t>(redundancy["desired_factor"]),
                        3);
      BOOST_CHECK_EQUAL(boost::any_cast<std::string>(redundancy["type"]),
                        "replication");
    }
    else
    {
      BOOST_CHECK_EQUAL(boost::any_cast<int64_t>(redundancy["desired_factor"]),
                        1);
      BOOST_CHECK_EQUAL(boost::any_cast<std::string>(redundancy["type"]),
                        "none");
    }
  }
}
#endif

template <typename T>
int
size(elle::reactor::Generator<T> const& g)
{
  int res = 0;
  for (auto const& m: elle::unconst(g))
  {
    (void)m;
    ++res;
  }
  return res;
};

class Local:
  public dht::consensus::Paxos::LocalPeer
{
public:
  using Super = dht::consensus::Paxos::LocalPeer;
  using Address = memo::model::Address;

  template <typename ... Args>
  Local(memo::model::doughnut::consensus::Paxos& paxos,
        int factor,
        bool rebalance_auto_expand,
        std::chrono::system_clock::duration node_timeout,
        memo::model::doughnut::Doughnut& dht,
        Address id,
        Args&& ... args)
    : memo::model::doughnut::Peer(dht, id)
    , Super(paxos,
            factor,
            rebalance_auto_expand,
            true,
            node_timeout,
            dht,
            id,
            std::forward<Args>(args)...)
    , _all_barrier()
    , _propose_barrier()
    , _propose_bypass(false)
    , _accept_barrier()
    , _accept_bypass(false)
    , _confirm_barrier()
    , _confirm_bypass(false)
    , _store_barrier()
  {
    this->_all_barrier.open();
    this->_propose_barrier.open();
    this->_accept_barrier.open();
    this->_confirm_barrier.open();
    this->_store_barrier.open();
  }

  virtual
  void
  store(blocks::Block const& block, memo::model::StoreMode mode) override
  {
    elle::reactor::wait(this->_store_barrier);
    Super::store(block, mode);
  }

  Paxos::PaxosServer::Response
  propose(PaxosServer::Quorum const& peers,
          Address address,
          PaxosClient::Proposal const& p,
          bool insert) override
  {
    this->_proposing(address, p);
    elle::reactor::wait(this->all_barrier());
    if (!this->_propose_bypass)
      elle::reactor::wait(this->propose_barrier());
    auto res = Super::propose(peers, address, p, insert);
    this->_proposed(address, p);
    return res;
  }

  PaxosClient::Proposal
  accept(PaxosServer::Quorum const& peers,
         Address address,
         PaxosClient::Proposal const& p,
         Value const& value) override
  {
    this->_accepting(address, p);
    elle::reactor::wait(this->all_barrier());
    if (!this->_accept_bypass)
      elle::reactor::wait(this->accept_barrier());
    auto res = Super::accept(peers, address, p, value);
    this->_accepted(address, p);
    return res;
  }

  void
  confirm(PaxosServer::Quorum const& peers,
          Address address,
          PaxosClient::Proposal const& p) override
  {
    this->_confirming(address, p);
    elle::reactor::wait(this->all_barrier());
    if (!this->_confirm_bypass)
      elle::reactor::wait(this->confirm_barrier());
    Super::confirm(peers, address, p);
    this->_confirmed(address, p);
  }

  void
  _disappeared_schedule_eviction(memo::model::Address id) override
  {
    ELLE_TRACE("%s: node %f disappeared, evict when signaled", this, id);
    this->_evict.connect([this, id] { this->_disappeared_evict(id); });
  }

  ELLE_ATTRIBUTE_RX(elle::reactor::Barrier, all_barrier);
  ELLE_ATTRIBUTE_RX(elle::reactor::Barrier, propose_barrier);
  ELLE_ATTRIBUTE_RW(bool, propose_bypass);
  using Hook =
    boost::signals2::signal<void(Address, PaxosClient::Proposal const&)>;
  ELLE_ATTRIBUTE_RX(Hook, proposing);
  ELLE_ATTRIBUTE_RX(Hook, proposed);
  ELLE_ATTRIBUTE_RX(elle::reactor::Barrier, accept_barrier);
  ELLE_ATTRIBUTE_RW(bool, accept_bypass);
  ELLE_ATTRIBUTE_RX(Hook, accepting);
  ELLE_ATTRIBUTE_RX(Hook, accepted);
  ELLE_ATTRIBUTE_RX(elle::reactor::Barrier, confirm_barrier);
  ELLE_ATTRIBUTE_RW(bool, confirm_bypass);
  ELLE_ATTRIBUTE_RX(Hook, confirming);
  ELLE_ATTRIBUTE_RX(Hook, confirmed);
  ELLE_ATTRIBUTE_RX(boost::signals2::signal<void()>, evict);
  ELLE_ATTRIBUTE_RX(elle::reactor::Barrier, store_barrier);
};

static constexpr
auto default_node_timeout = 1s;

class InstrumentedPaxos
  : public dht::consensus::Paxos
{
  using Super = dht::consensus::Paxos;
  using Super::Super;
  std::unique_ptr<dht::Local>
  make_local(
    boost::optional<int> port,
    boost::optional<boost::asio::ip::address> listen,
    std::unique_ptr<memo::silo::Silo> storage) override
  {
    return std::make_unique<Local>(
      *this,
      this->factor(),
      this->rebalance_auto_expand(),
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
        default_node_timeout),
      this->doughnut(),
      this->doughnut().id(),
      std::move(storage),
      port.value_or(0));
  }
};

dht::Doughnut::ConsensusBuilder
instrument(int factor, bool rebalance_auto_expand = true)
{
  return [factor, rebalance_auto_expand] (dht::Doughnut& d)
    -> std::unique_ptr<dht::consensus::Consensus>
  {
    return std::make_unique<InstrumentedPaxos>(
      dht::consensus::doughnut = d,
      dht::consensus::replication_factor = factor,
      dht::consensus::rebalance_auto_expand = rebalance_auto_expand);
  };
}

auto const make_dht = [] (int n, bool rebalance_auto_expand = false)
{
  return std::make_unique<DHT>(
    id = special_id(n + 10),
    dht::consensus_builder = instrument(3, rebalance_auto_expand));
};

namespace rebalancing
{
  ELLE_TEST_SCHEDULED(extend_and_write)
  {
    auto dht_a = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    auto b1 = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 1")
    {
      b1->data(std::string("extend_and_write 1"));
      dht_a.dht->seal_and_insert(*b1);
    }
    dht_b.overlay->connect(*dht_a.overlay);
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b1->address(), 3)), 1u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b1->address(), 3)), 1u);
    auto& paxos_a =
      dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
    ELLE_LOG("rebalance block to quorum of 2")
      paxos_a.rebalance(b1->address());
    ELLE_LOG("read block on quorum of 2")
    {
      b1 = std::dynamic_pointer_cast<blocks::MutableBlock>(
        dht_a.dht->fetch(b1->address()));
      BOOST_TEST(b1->data() == elle::Buffer("extend_and_write 1"));
      auto bb = dht_b.dht->fetch(b1->address());
      BOOST_TEST(bb->data() == elle::Buffer("extend_and_write 1"));
    }
    ELLE_LOG("write block to quorum of 2")
    {
      b1->data(std::string("extend_and_write 1 bis"));
      dht_a.dht->seal_and_update(*b1);
    }
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b1->address(), 3)), 2u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b1->address(), 3)), 2u);
  }

  template<typename BT>
  void
  run_extend_shrink_and_write()
  {
    DHT dht_a(id = special_id(1),
              dht::consensus::rebalance_auto_expand = true,
              dht::consensus::node_timeout = 0s);
    DHT dht_b(id = special_id(2),
              dht::consensus::rebalance_auto_expand = true,
              dht::consensus::node_timeout = 0s);
    DHT dht_c(id = special_id(3),
              dht::consensus::rebalance_auto_expand = false);
    dht_a.overlay->connect(*dht_b.overlay);
    dht_a.overlay->connect(*dht_c.overlay);
    dht_b.overlay->connect(*dht_c.overlay);
    auto& paxos_a =
      dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
    auto b1 = dht_a.dht->make_block<BT>();
    ELLE_LOG("write block to quorum of 3")
    {
      b1->data(std::string("extend_and_write 1"));
      dht_a.dht->seal_and_insert(*b1);
      BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b1->address(), 3)), 3u);
      BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b1->address(), 3)), 3u);
    }
    ELLE_LOG("rebalance block to quorum of 2")
    {
      dht_c.overlay->disconnect(*dht_a.overlay);
      dht_c.overlay->disconnect(*dht_b.overlay);
      paxos_a.rebalance(b1->address());
      BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b1->address(), 3)), 2u);
      BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b1->address(), 3)), 2u);
    }
    DHT dht_d(id = special_id(4),
              dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("rebalance block to quorum of 3")
    {
      dht_d.overlay->connect(*dht_a.overlay);
      dht_d.overlay->connect(*dht_b.overlay);
      paxos_a.rebalance(b1->address());
    }
    ELLE_LOG("write block to quorum of 3")
    {
      b1->data(std::string("extend_and_write 1 bis"));
      try
      {
        dht_a.dht->seal_and_update(*b1);
      }
      catch (memo::model::Conflict const& c)
      {
        ELLE_LOG("resolve conflict")
        {
          dynamic_cast<memo::model::blocks::MutableBlock&>(*c.current())
            .data(std::string("extend_and_write 1 bis"));
          dht_a.dht->seal_and_update(*c.current());
        }
      }
      BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b1->address(), 3)), 3u);
      BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b1->address(), 3)), 3u);
    }
  }

  ELLE_TEST_SCHEDULED(extend_shrink_and_write)
  {
    run_extend_shrink_and_write<memo::model::blocks::MutableBlock>();
    run_extend_shrink_and_write<memo::model::blocks::ACLBlock>();
  }

  ELLE_TEST_SCHEDULED(shrink_and_write)
  {
    auto dht_a = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    dht_b.overlay->connect(*dht_a.overlay);
    auto b1 = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 2")
    {
      b1->data(std::string("shrink_kill_and_write 1"));
      dht_a.dht->seal_and_insert(*b1);
    }
    auto& paxos_a =
      dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
    ELLE_LOG("rebalance block to quorum of 1")
      paxos_a.rebalance(b1->address(), {dht_a.dht->id()});
    ELLE_LOG("write block to quorum of 1")
    {
      b1->data(std::string("extend_and_write 2"));
      dht_a.dht->seal_and_update(*b1);
    }
  }

  ELLE_TEST_SCHEDULED(shrink_kill_and_write)
  {
    auto dht_a = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    dht_b.overlay->connect(*dht_a.overlay);
    auto b1 = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 2")
    {
      b1->data(std::string("shrink_kill_and_write 1"));
      dht_a.dht->seal_and_insert(*b1);
    }
    auto& paxos_a =
      dynamic_cast<dht::consensus::Paxos&>(*dht_a.dht->consensus());
    ELLE_LOG("rebalance block to quorum of 1")
      paxos_a.rebalance(b1->address(), {dht_a.dht->id()});
    dht_b.overlay->disconnect(*dht_a.overlay);
    ELLE_LOG("write block to quorum of 1")
    {
      b1->data(std::string("extend_and_write 2"));
      dht_a.dht->seal_and_update(*b1);
    }
  }

  ELLE_TEST_SCHEDULED(quorum_duel_1)
  {
    auto dht_a = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %f", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %f", dht_b.dht->id());
    dht_b.overlay->connect_recursive(*dht_a.overlay);
    auto dht_c = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("third DHT: %f", dht_c.dht->id());
    dht_c.overlay->connect_recursive(*dht_a.overlay);
    auto b = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 3")
    {
      b->data(std::string("quorum_duel"));
      dht_a.dht->seal_and_insert(*b);
    }
    BOOST_CHECK_EQUAL(mutable_block_count(dht_c.overlay->blocks()), 1u);
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
      BOOST_CHECK_EQUAL(mutable_block_count(dht_c.overlay->blocks()), 1u);
      b->data(std::string("quorum_duel_edited"));
      dht_c.dht->seal_and_update(*b);
    }
  }

  ELLE_TEST_SCHEDULED(quorum_duel_2)
  {
    auto dht_a = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("first DHT: %f", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("second DHT: %f", dht_b.dht->id());
    dht_b.overlay->connect_recursive(*dht_a.overlay);
    auto dht_c = DHT(dht::consensus::rebalance_auto_expand = false);
    ELLE_LOG("third DHT: %f", dht_c.dht->id());
    dht_c.overlay->connect_recursive(*dht_a.overlay);
    auto b = dht_a.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to quorum of 3")
    {
      b->data(std::string("quorum_duel"));
      dht_a.dht->seal_and_insert(*b);
    }
    BOOST_CHECK_EQUAL(mutable_block_count(dht_c.overlay->blocks()), 1u);
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
      BOOST_CHECK_EQUAL(mutable_block_count(dht_c.overlay->blocks()), 1u);
      b->data(std::string("quorum_duel_edited"));
      dht_c.dht->seal_and_update(*b);
    }
  }

  class VersionHop:
    public memo::model::ConflictResolver
  {
  public:
    VersionHop(blocks::Block& previous)
      : _previous(previous.data())
    {}

    std::unique_ptr<blocks::Block>
    operator () (blocks::Block& failed,
                 blocks::Block& current) override
    {
      BOOST_CHECK_EQUAL(current.data(), this->_previous);
      return failed.clone();
    }

    void
    serialize(elle::serialization::Serializer& s,
              elle::Version const&) override
    {
      s.serialize("previous", this->_previous);
    }

    std::string
    description() const override
    {
      return "";
    }

    ELLE_ATTRIBUTE_R(elle::Buffer, previous);
  };

  static
  std::unique_ptr<blocks::Block>
  make_block(DHT& client, bool immutable, std::string data_)
  {
    auto data = elle::Buffer(std::move(data_));
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
    auto dht_a = DHT(id = special_id(10),
                     dht::consensus_builder = instrument(2));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    auto dht_b = DHT(id = special_id(11),
                     dht::consensus_builder = instrument(2));
    auto& local_b = dynamic_cast<Local&>(*dht_b.dht->local());
    local_b.store_barrier().close();
    dht_b.overlay->connect(*dht_a.overlay);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    auto client = DHT(id = special_id(11), storage = nullptr);
    client.overlay->connect(*dht_a.overlay);
    auto b = make_block(client, immutable, "expand_new_block");
    ELLE_LOG("write block to one DHT")
      client.dht->seal_and_insert(*b);
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 2)), 1u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 2)), 1u);
    local_b.store_barrier().open();
    ELLE_LOG("wait for rebalancing")
      elle::reactor::wait(local_a.rebalanced(), b->address());
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 2)), 2u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 2)), 2u);
    ELLE_LOG("disconnect second DHT")
      dht_b.overlay->disconnect(*dht_a.overlay);
    ELLE_LOG("read block from second DHT")
      BOOST_CHECK_EQUAL(dht_b.dht->fetch(b->address())->data(), b->data());
  }

  ELLE_TEST_SCHEDULED(expand_newcomer, (bool, immutable))
  {
    auto dht_a = DHT(id = special_id(10),
                     dht::consensus_builder = instrument(3));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    auto dht_b = DHT(id = special_id(11),
                     dht::consensus_builder = instrument(3));
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    auto b = make_block(dht_a, immutable, "expand_newcomer");
    ELLE_LOG("write block to first DHT")
      dht_a.dht->seal_and_insert(*b);
    // Block the new quorum election to check the balancing is done in
    // background.
    local_a.propose_barrier().close();
    // Wait until the first automatic expansion fails.
    elle::reactor::wait(dht_a.overlay->looked_up(), b->address());
    ELLE_LOG("connect second DHT")
      dht_b.overlay->connect(*dht_a.overlay);
    if (!immutable)
    {
      elle::reactor::wait(
        local_a.proposing(),
        [&] (Address const& a, Paxos::PaxosClient::Proposal const&)
        {
          return a == b->address();
        });
      BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 3)), 1u);
      BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 3)), 1u);
      // Insert another block, to check iterator invalidation while balancing.
      ELLE_LOG("write other block to first DHT")
      {
        local_a.propose_bypass(true);
        auto perturbate = dht_a.dht->make_block<blocks::MutableBlock>();
      perturbate->data(std::string("booh!"));
      dht_a.dht->seal_and_insert(*perturbate);
      }
      local_a.propose_barrier().open();
    }
    ELLE_LOG("wait for rebalancing")
      elle::reactor::wait(local_a.rebalanced(), b->address());
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 3)), 2u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 3)), 2u);
    if (!immutable)
    {
      auto& mb = dynamic_cast<blocks::MutableBlock&>(*b);
      ELLE_LOG("write block to both DHTs")
      {
        auto resolver = std::make_unique<VersionHop>(mb);
        mb.data(std::string("expand'"));
        dht_b.dht->seal_and_update(mb, std::move(resolver));
      }
    }
    ELLE_LOG("disconnect second DHT")
      dht_b.overlay->disconnect(*dht_a.overlay);
    ELLE_LOG("read block from second DHT")
      BOOST_CHECK_EQUAL(dht_b.dht->fetch(b->address())->data(), b->data());
  }

  ELLE_TEST_SCHEDULED(expand_concurrent)
  {
    auto dht_a = DHT(dht::consensus_builder = instrument(3));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %s", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus_builder = instrument(3));
    auto& local_b = dynamic_cast<Local&>(*dht_b.dht->local());
    dht_b.overlay->connect(*dht_a.overlay);
    ELLE_LOG("second DHT: %s", dht_b.dht->id());
    auto dht_c = DHT(dht::consensus_builder = instrument(3));
    dht_c.overlay->connect(*dht_a.overlay);
    dht_c.overlay->connect(*dht_b.overlay);
    ELLE_LOG("third DHT: %s", dht_b.dht->id());
    auto client = DHT(storage = nullptr);
    client.overlay->connect(*dht_a.overlay);
    client.overlay->connect(*dht_b.overlay);
    auto b = client.dht->make_block<blocks::MutableBlock>();
    ELLE_LOG("write block to two DHT")
    {
      b->data(std::string("expand"));
      client.dht->seal_and_insert(*b);
    }
    ELLE_LOG("wait for rebalancing")
    {
      boost::signals2::signal<void(memo::model::Address)> rebalanced;
      boost::signals2::scoped_connection c_a =
        local_a.rebalanced().connect(rebalanced);
      boost::signals2::scoped_connection c_b =
        local_b.rebalanced().connect(rebalanced);
      elle::reactor::wait(rebalanced, b->address());
    }
    BOOST_CHECK_EQUAL(size(dht_a.overlay->lookup(b->address(), 3)), 3u);
    BOOST_CHECK_EQUAL(size(dht_b.overlay->lookup(b->address(), 3)), 3u);
    BOOST_CHECK_EQUAL(size(dht_c.overlay->lookup(b->address(), 3)), 3u);
  }

  ELLE_TEST_SCHEDULED(expand_from_disk, (bool, immutable))
  {
    memo::silo::Memory::Blocks storage_a;
    memo::model::Address address;
    auto id_a = memo::model::Address::random();
    ELLE_LOG("create block with 1 DHT")
    {
      auto dht_a = DHT(id = id_a,
                dht::consensus_builder = instrument(3),
                storage = std::make_unique<Memory>(storage_a));
      auto block = make_block(dht_a, immutable, "expand_from_disk");
      address = block->address();
      dht_a.dht->insert(std::move(block));
    }
    BOOST_CHECK(storage_a.find(address) != storage_a.end());
    ELLE_LOG("restart with 2 DHTs")
    {
      auto dht_a = DHT(id = id_a,
                dht::consensus_builder = instrument(3),
                storage = std::make_unique<Memory>(storage_a));
      auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
      auto dht_b = DHT(dht::consensus_builder = instrument(3));
      dht_b.overlay->connect(*dht_a.overlay);
      elle::reactor::wait(local_a.rebalanced(), address);
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
      dht_a.dht->seal_and_insert(*b1);
    }
    dht_b.overlay->connect(*dht_a.overlay);
  }

  ELLE_TEST_SCHEDULED(evict_faulty, (bool, immutable))
  {
    auto dht_a = DHT(dht::consensus_builder = instrument(3));
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %f", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus_builder = instrument(3));
    auto& local_b = dynamic_cast<Local&>(*dht_b.dht->local());
    dht_b.overlay->connect(*dht_a.overlay);
    ELLE_LOG("second DHT: %f", dht_b.dht->id());
    auto dht_c = DHT(dht::consensus_builder = instrument(3));
    dht_c.overlay->connect(*dht_a.overlay);
    dht_c.overlay->connect(*dht_b.overlay);
    ELLE_LOG("third DHT: %f", dht_c.dht->id());
    auto b = make_block(dht_a, immutable, "evict_faulty");
    ELLE_LOG("write block")
      dht_a.dht->seal_and_insert(*b);
    auto dht_d = DHT(dht::consensus_builder = instrument(3));
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
      boost::signals2::signal<void(memo::model::Address)> rebalanced;
      boost::signals2::scoped_connection c_a =
        local_a.rebalanced().connect(rebalanced);
      boost::signals2::scoped_connection c_b =
        local_b.rebalanced().connect(rebalanced);
      elle::reactor::wait(rebalanced, b->address());
    }
    ELLE_LOG("disconnect first DHT")
      dht_a.overlay->disconnect_all();
    ELLE_LOG("read block")
      BOOST_CHECK_EQUAL(dht_b.dht->fetch(b->address())->data(), b->data());
  }

  ELLE_TEST_SCHEDULED(evict_removed_blocks, (bool, immutable))
  {
    auto dht_a = DHT(dht::consensus_builder = instrument(3),
                     dht::consensus::rebalance_auto_expand = false);
    auto& local_a = dynamic_cast<Local&>(*dht_a.dht->local());
    ELLE_LOG("first DHT: %f", dht_a.dht->id());
    auto dht_b = DHT(dht::consensus_builder = instrument(3),
                     dht::consensus::rebalance_auto_expand = false);
    dht_b.overlay->connect(*dht_a.overlay);
    ELLE_LOG("second DHT: %f", dht_b.dht->id());
    auto ba = make_block(dht_a, immutable, "evict_faulty");
    auto bb = make_block(dht_a, immutable, "evict_faulty");
    auto bc = make_block(dht_a, immutable, "evict_faulty");
    ELLE_LOG("write blocks");
    {
      dht_a.dht->seal_and_insert(*ba);
      dht_a.dht->seal_and_insert(*bb);
      dht_a.dht->seal_and_insert(*bc);
    }
    ELLE_LOG("remove block")
      dht_a.dht->remove(bb->address());
    ELLE_LOG("disconnect second dht")
    {
      dht_b.overlay->disconnect_all();
      local_a.evict()();
    }
  }

  auto const make_resign_dht = [] (int n, bool rebalance_auto_expand = true)
  {
    return std::make_unique<DHT>(
      id = special_id(n + 10),
      dht::consensus_builder = instrument(3),
      dht::resign_on_shutdown = true,
      dht::consensus::rebalance_auto_expand = rebalance_auto_expand);
  };

  ELLE_TEST_SCHEDULED(resign)
  {
    auto dht_a = make_resign_dht(0);
    auto dht_b = make_resign_dht(1);
    auto dht_c = make_resign_dht(2);
    dht_b->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_b->overlay);
    auto ba = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("resignation1"));
    auto bb = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("resignation2"));
    auto bc = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("resignation3"));
    ELLE_LOG("write blocks");
    {
      dht_a->dht->seal_and_insert(*ba);
      dht_a->dht->seal_and_insert(*bb);
      dht_a->dht->seal_and_insert(*bc);
    }
    ELLE_LOG("disconnect third dht")
      dht_c.reset();
    ELLE_LOG("disconnect second dht")
      dht_b.reset();
    ELLE_LOG("update blocks")
    {
      ba->data(std::string("resignation1'"));
      bb->data(std::string("resignation2'"));
      bc->data(std::string("resignation3'"));
      dht_a->dht->seal_and_insert(*ba);
      dht_a->dht->seal_and_insert(*bb);
      dht_a->dht->seal_and_insert(*bc);
    }
  }

  // If there was a conflict in quorum expansion, both nodes would consider that
  // the other rebalanced to the correct quorum and none would propagate the
  // block. Both should.
  ELLE_TEST_SCHEDULED(conflict)
  {
    auto dht_a = make_resign_dht(0);
    auto dht_b = make_resign_dht(1);
    auto dht_c = make_resign_dht(2);
    dht_b->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_b->overlay);
    auto block = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("conflict"));
    ELLE_LOG("write block");
      dht_a->dht->seal_and_insert(*block);
    auto dht_d = make_resign_dht(3);
    dht_d->overlay->connect(*dht_a->overlay);
    dht_d->overlay->connect(*dht_b->overlay);
    dht_d->overlay->connect(*dht_c->overlay);
    auto& local_a = dynamic_cast<Local&>(*dht_a->dht->local());
    auto& local_b = dynamic_cast<Local&>(*dht_b->dht->local());
    // Ensure we have a conflict when pickin the quorum.
    elle::reactor::Barrier b_proposal;
    elle::reactor::Barrier a_acceptance;
    elle::reactor::Barrier confirmation_a;
    elle::reactor::Barrier confirmation_b;
    for (auto l: {&local_a, &local_b})
      l->proposing().connect(
        [&] (Address const&, Paxos::PaxosClient::Proposal const& p)
        {
          if (p.sender == dht_b->dht->id())
          {
            ELLE_LOG("block B proposal");
            elle::reactor::wait(b_proposal);
          }
        });
    local_b.accepting().connect(
        [&] (Address const&, Paxos::PaxosClient::Proposal const& p)
        {
          if (p.sender == dht_a->dht->id())
          {
            ELLE_LOG("block A acceptance on B");
            elle::reactor::wait(a_acceptance);
          }
        });
    auto a_stuck = elle::reactor::waiter(
      local_a.accepted(),
      [&] (Address, Paxos::PaxosClient::Proposal const& p)
      { return p.sender == dht_a->dht->id(); });
    ELLE_LOG("disconnect C")
      dht_c.reset();
    ELLE_LOG("wait until A has accepted on A")
      elle::reactor::wait(a_stuck);
    ELLE_LOG("release B")
      b_proposal.open();
    auto wait_a =
      elle::reactor::waiter(local_b.rebalanced(), block->address());
    auto wait_b =
      elle::reactor::waiter(local_a.rebalanced(), block->address());
    ELLE_LOG("wait until B has picked the quorum");
    boost::signals2::connection connection = local_a.confirming().connect(
      [&] (Address, Paxos::PaxosClient::Proposal const& p)
      {
        if (p.sender == dht_b->dht->id())
        {
          ELLE_LOG("release A")
            a_acceptance.open();
          ELLE_LOG("wait until A has picked the quorum too")
            elle::reactor::wait(
              local_a.proposed(),
              [&] (Address, Paxos::PaxosClient::Proposal const& p)
              { return p.sender == dht_a->dht->id(); });
          connection.disconnect();
          for (auto l: {&local_a, &local_b})
          {
            l->proposing().disconnect_all_slots();
            l->accepting().disconnect_all_slots();
          }
        }
      });
    elle::reactor::wait(elle::reactor::Waitables{&wait_a, &wait_b});
  }

  ELLE_TEST_SCHEDULED(conflict_quorum)
  {
    auto dht_a = make_resign_dht(0);
    auto dht_b = make_resign_dht(1);
    auto dht_c = make_resign_dht(2);
    dht_b->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_b->overlay);
    auto block = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("conflict"));
    ELLE_LOG("write block");
      dht_a->dht->seal_and_insert(*block);
    auto dht_d = make_resign_dht(3);
    dht_d->overlay->connect(*dht_a->overlay);
    dht_d->overlay->connect(*dht_b->overlay);
    dht_d->overlay->connect(*dht_c->overlay);
    auto& local_a = dynamic_cast<Local&>(*dht_a->dht->local());
    auto& local_b = dynamic_cast<Local&>(*dht_b->dht->local());
    int count = 0;
    elle::reactor::Barrier confirm;
    local_a.confirming().connect(
      [&] (Address const&, Paxos::PaxosClient::Proposal const& p)
      {
        if (++count == 1)
          elle::reactor::wait(confirm);
        else
          confirm.open();
      });
    auto rebalanced =
      elle::reactor::waiter(local_b.rebalanced(), block->address());
    ELLE_LOG("disconnect third dht")
      dht_c.reset();
    elle::reactor::wait(rebalanced);
  }

  ELLE_TEST_SCHEDULED(missing_block)
  {
    elle::reactor::Barrier confirm;
    auto dht_a = make_resign_dht(0);
    auto dht_b = make_resign_dht(1);
    auto dht_c = make_resign_dht(2);
    dht_b->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_b->overlay);
    auto block = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("missing"));
    ELLE_LOG("write block");
      dht_a->dht->seal_and_insert(*block);
    auto& local_a = dynamic_cast<Local&>(*dht_a->dht->local());
    auto& local_b = dynamic_cast<Local&>(*dht_b->dht->local());
    auto& local_c = dynamic_cast<Local&>(*dht_c->dht->local());
    for (auto l: {&local_a, &local_b})
      l->confirming().connect(
        [&] (Address const&, Paxos::PaxosClient::Proposal const& p)
        {
          elle::reactor::wait(confirm);
        });
    auto deleted = elle::reactor::waiter(
      local_c.confirmed(),
      [&] (Address a, Paxos::PaxosClient::Proposal const&)
      { BOOST_TEST(a == block->address()); return true; });
    elle::reactor::Thread resign(
      "resign",
      [&]
      {
        ELLE_LOG("resign third dht")
          dht_c->dht->resign();
      });
    elle::reactor::wait(deleted);
    ELLE_LOG("fetch block")
    {
      auto fetched = dht_a->dht->fetch(block->address());
      BOOST_TEST(fetched->data() == block->data());
    }
    confirm.open();
  }

  ELLE_TEST_SCHEDULED(resign_insist)
  {
    auto dht_a = make_resign_dht(0);
    auto dht_b = make_resign_dht(1);
    auto dht_c = make_resign_dht(2);
    dht_b->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_b->overlay);
    auto block = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("resign_insist"));
    ELLE_LOG("write block")
      dht_a->dht->seal_and_insert(*block);
    auto& local_a = dynamic_cast<Local&>(*dht_a->dht->local());
    auto& local_b = dynamic_cast<Local&>(*dht_b->dht->local());
    auto& local_c = dynamic_cast<Local&>(*dht_c->dht->local());
    int count = 0;
    for (auto l: {&local_a, &local_b})
      l->proposing().connect(
        [&] (Address const&, Paxos::PaxosClient::Proposal const& p)
        {
          if (++count <= 12)
            throw elle::athena::paxos::Unavailable();
        });
    auto rebalanced =
      elle::reactor::waiter(local_c.rebalanced(), block->address());
    dht_c.reset();
    BOOST_TEST(count > 12);
    elle::reactor::wait(rebalanced);
  }

  // Perform a full rotation, transferring a block to a quorum with none of the
  // original owners. New owner used to not replicate immutable blocks. The
  // expand version evicts a node and then introduce a new one, to check the
  // block is expanded. The !expand version introduce a new node, then evicts
  // one to check the block is rebalanced right away.
  ELLE_TEST_SCHEDULED(evict_chain, (bool, expand))
  {
    std::vector<std::unique_ptr<DHT>> dhts;
    auto const make = [&] (int i)
      {
        auto dht = make_dht(i, expand);
        for (auto const& d: dhts)
          dht->overlay->connect(*d->overlay);
        auto const id = dht->dht->id();
        dhts.emplace_back(std::move(dht));
        return id;
      };
    make(0);
    make(1);
    make(2);
    auto block = dhts[0]->dht->make_block<blocks::ImmutableBlock>(
      std::string("resign_chain"));
    ELLE_LOG("write block")
      dhts[0]->dht->seal_and_insert(*block);
    BOOST_TEST(size(dhts[0]->overlay->lookup(block->address(), 3)) == 3u);
    for (auto i = 3; i < 32; ++i)
      ELLE_LOG("rotate to a new DHT")
      {
        decltype(make(i)) inserted;
        if (!expand)
          inserted = make(i);
        auto erased = [&]
          {
            auto it = *std::begin(elle::pick_n(1, dhts));
            auto id = (*it)->dht->id();
            dhts.erase(it);
            return id;
          }();
        auto& local = dynamic_cast<Local&>(*dhts[0]->dht->local());
        auto not_rebalanced =
          elle::reactor::waiter(local.under_replicated(), block->address(), 2);
        local.evict()();
        if (expand)
        {
          ELLE_LOG("wait until the block in under-replicated")
            elle::reactor::wait(not_rebalanced);
          inserted = make(i);
        }
        if (erased != inserted)
          ELLE_LOG("wait until the block is rebalanced")
            elle::reactor::wait(local.rebalanced(), block->address());
        BOOST_TEST(size(dhts[0]->overlay->lookup(block->address(), 3)) == 3u);
      }
  }

  ELLE_TEST_SCHEDULED(update_while_evicting)
  {
    auto dht_a = make_dht(0);
    auto dht_b = make_dht(1);
    dht_b->overlay->connect(*dht_a->overlay);
    auto dht_c = make_dht(2);
    auto id_c = dht_c->dht->id();
    auto& local_c = dynamic_cast<Local&>(*dht_c->dht->local());
    dht_c->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_b->overlay);
    auto block = [&]
      {
        ELLE_LOG_SCOPE("write block");
        auto b = dht_a->dht->make_block<blocks::MutableBlock>(
          std::string("update_while_evicting"));
        dht_a->dht->seal_and_insert(*b);
        return b;
      }();
    elle::reactor::Barrier finish_rebalancing;
    elle::reactor::Barrier update;
    local_c.proposing().connect(
      [&] (Address, Paxos::PaxosClient::Proposal const& p)
      {
        if (p.sender == dht_a->dht->id())
          elle::reactor::wait(update);
      });
    // local_c.proposed().connect(
    //   [&] (Address, Paxos::PaxosClient::Proposal const& p)
    //   {
    //     if (p.sender == dht_a->dht->id())
    //     {
    //       ELLE_ERR("BBY");
    //       finish_rebalancing.open();
    //     }
    //   });
    local_c.confirmed().connect(
      [&] (Address, Paxos::PaxosClient::Proposal const& p)
      {
        if (p.sender == id_c)
        {
          update.open();
          // elle::reactor::wait(finish_rebalancing);
        }
      });
    ELLE_LOG("resign and update");
    elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& scope)
    {
      scope.run_background(
        elle::print("update %f", dht_c),
        [&]
        {
          block->data(elle::Buffer("update_while_evicting'"));
          dht_a->dht->seal_and_update(*block);
          ELLE_WARN("BYE 1");
        });
      scope.run_background(
        elle::print("resign %f", dht_c),
        [&]
        {
          dht_c->dht->resign();
          ELLE_WARN("BYE 2");
        });
      elle::reactor::wait(scope);
    };
  }

  ELLE_TEST_SCHEDULED(peeking_node)
  {
    auto dht_a = make_dht(0, true);
    auto& local_a = dynamic_cast<Local&>(*dht_a->dht->local());
    auto dht_b = make_dht(1);
    dht_b->overlay->connect(*dht_a->overlay);
    auto block = [&]
      {
        ELLE_LOG_SCOPE("write block");
        auto b = dht_a->dht->make_block<blocks::MutableBlock>(
          std::string("peeking_node"));
        dht_a->dht->seal_and_insert(*b);
        return b;
      }();
    // wait for initial rebalancing to fail
    elle::reactor::wait(dht_a->overlay->looked_up(), block->address());
    auto waiter = elle::reactor::waiter(local_a.rebalanced(), block->address());
    ELLE_LOG("peek DHT 0x0c")
    {
      auto dht_c = make_dht(2);
      dht_a->overlay->connect(*dht_c->overlay);
      dht_b->overlay->connect(*dht_c->overlay);
    }
    // local_a.evict()();
    auto dht_d = make_dht(3);
    auto& local_d = dynamic_cast<Local&>(*dht_a->dht->local());
    dht_a->overlay->connect(*dht_d->overlay);
    dht_b->overlay->connect(*dht_d->overlay);
    elle::reactor::wait(waiter);
    BOOST_CHECK_NO_THROW(local_d.storage()->get(block->address()));
  }
}

ELLE_TEST_SCHEDULED(CHB_unavailable)
{
  auto a = make_dht(0, true);
  auto& local_a = dynamic_cast<Local&>(*a->dht->local());
  auto b = make_dht(1, false);
  b->overlay->connect(*a->overlay);
  auto c = make_dht(2, false);
  auto& local_c = dynamic_cast<Local&>(*c->dht->local());
  c->overlay->connect(*b->overlay);
  c->overlay->connect(*a->overlay);
  local_c.store_barrier().raise<elle::athena::paxos::Unavailable>();
  auto block = a->dht->make_block<blocks::ImmutableBlock>(
    elle::Buffer("CHB_unavailable"));
  auto rebalanced =
    elle::reactor::waiter(local_a.rebalanced(), block->address());
  a->dht->seal_and_insert(*block);
  local_c.store_barrier().open();
  BOOST_TEST(size(a->overlay->lookup(block->address(), 3)) == 2u);
  elle::reactor::wait(rebalanced);
  BOOST_TEST(size(a->overlay->lookup(block->address(), 3)) == 3u);
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
  ctx.set<dht::Doughnut*>(d);
  auto res =
    elle::serialization::binary::deserialize<std::unique_ptr<blocks::Block>>(
      is, true, ctx);
  b.reset(res.release());
}

ELLE_TEST_SCHEDULED(admin_keys)
{
  auto owner_key = elle::cryptography::rsa::keypair::generate(512);
  auto dht = DHT(keys=owner_key, owner=owner_key);
  auto client = DHT(storage = nullptr, keys=owner_key, owner=owner_key);
  client.overlay->connect(*dht.overlay);
  auto b0 = client.dht->make_block<blocks::ACLBlock>();
  b0->data(std::string("foo"));
  client.dht->seal_and_insert(*b0);
  auto b1 = client.dht->make_block<blocks::ACLBlock>();
  b1->data(std::string("foo"));
  client.dht->seal_and_insert(*b1);
  auto b2 = client.dht->fetch(b1->address());
  no_cheating(client.dht.get(), b2);
  BOOST_CHECK_EQUAL(b2->data().string(), "foo");
  // set server-side adm key but don't tell the client
  auto admin = elle::cryptography::rsa::keypair::generate(512);
  dht.dht->admin_keys().r.push_back(admin.K());
  dynamic_cast<memo::model::blocks::MutableBlock*>(b2.get())->
    data(std::string("bar"));
  BOOST_CHECK_THROW(client.dht->seal_and_update(*b2), std::exception);
  auto b3 = client.dht->make_block<blocks::ACLBlock>();
  b3->data(std::string("baz"));
  BOOST_CHECK_THROW(client.dht->seal_and_insert(*b3), std::exception);
  // tell the client
  client.dht->admin_keys().r.push_back(admin.K());
  b3 = client.dht->make_block<blocks::ACLBlock>();
  b3->data(std::string("baz"));
  BOOST_CHECK_NO_THROW(client.dht->seal_and_insert(*b3));
  // check admin can actually read the block
  auto cadm = DHT(storage = nullptr, keys=admin, owner=owner_key);
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
  cadm.dht->seal_and_insert(*ba);
  auto ba2 = cadm.dht->fetch(ba->address());
  no_cheating(cadm.dht.get(), ba2);
  BOOST_CHECK_EQUAL(ba2->data(), std::string("foo"));
  auto ba3 = client.dht->fetch(ba->address());
  no_cheating(client.dht.get(), ba3);
  BOOST_CHECK_EQUAL(ba3->data(), std::string("foo"));
  dynamic_cast<memo::model::blocks::MutableBlock*>(ba3.get())->data(
    std::string("bar"));
  client.dht->seal_and_update(*ba3);
  auto ba4 = cadm.dht->fetch(ba->address());
  no_cheating(cadm.dht.get(), ba4);
  BOOST_CHECK_EQUAL(ba4->data(), std::string("bar"));
  // try to change admin user's permissions
  auto b_perm = dht.dht->make_block<blocks::ACLBlock>();
  b_perm->data(std::string("admin user data"));
  dht.dht->seal_and_insert(*b_perm);
  auto fetched_b_perm = dht.dht->fetch(b_perm->address());
  no_cheating(dht.dht.get(), fetched_b_perm);
  b_perm.reset(dynamic_cast<blocks::ACLBlock*>(fetched_b_perm.release()));
  BOOST_CHECK_THROW(
    b_perm->set_permissions(
      *dht.dht->make_user(elle::serialization::json::serialize(admin.K())),
    false, false), elle::Error);
  // check group admin key
  auto gadmin = elle::cryptography::rsa::keypair::generate(512);
  auto cadmg = DHT(storage = nullptr, keys=gadmin, owner=owner_key);
  cadmg.overlay->connect(*dht.overlay);
  std::unique_ptr<elle::cryptography::rsa::PublicKey> g_K;
  {
    dht::Group g(*dht.dht, "g");
    g.create();
    g.add_member(*cadmg.dht->make_user(elle::serialization::json::serialize(
      gadmin.K())));
    cadmg.dht->admin_keys().group_r.push_back(g.public_control_key());
    dht.dht->admin_keys().group_r.push_back(g.public_control_key());
    client.dht->admin_keys().group_r.push_back(g.public_control_key());
    g_K.reset(
      new elle::cryptography::rsa::PublicKey(g.public_control_key()));
  }
  auto bg = client.dht->make_block<blocks::ACLBlock>();
  bg->data(std::string("baz"));
  client.dht->seal_and_insert(*bg);
  auto bg2 = cadmg.dht->fetch(bg->address());
  BOOST_CHECK_EQUAL(bg2->data(), std::string("baz"));
  // try to change admin group's permissions
  auto bg_perm = cadmg.dht->fetch(bg->address());
  // no_cheating(cadmg.dht.get(), bg_perm);
  BOOST_CHECK_THROW(
    dynamic_cast<blocks::ACLBlock*>(bg_perm.get())->set_permissions(
      *cadmg.dht->make_user(elle::serialization::json::serialize(g_K)),
    false, false), elle::Error);
}

ELLE_TEST_SCHEDULED(disabled_crypto)
{
  auto const key = elle::cryptography::rsa::keypair::generate(key_size());
  auto const eopts = dht::EncryptOptions{false, false, false};
  DHTs dhts(true, encrypt_options = eopts, keys_a = key, keys_b=key, keys_c = key);
  auto b = dhts.dht_a->make_block<blocks::ACLBlock>(elle::Buffer("canard", 6));
  auto baddr = b->address();
  dhts.dht_a->insert(std::move(b));
  auto bc = dhts.dht_b->fetch(baddr);
  BOOST_CHECK_EQUAL(bc->data(), "canard");
  auto bi = dhts.dht_a->make_block<blocks::ImmutableBlock>(elle::Buffer("canard", 6));
  auto biaddr = bi->address();
  dhts.dht_a->insert(std::move(bi));
  auto bic = dhts.dht_b->fetch(biaddr);
  BOOST_CHECK_EQUAL(bic->data(), "canard");
}

ELLE_TEST_SCHEDULED(tombstones)
{
  auto dht_a = make_dht(0);
  auto dht_b = make_dht(1);
  memo::silo::Memory::Blocks storage_c;
  auto make_dht_c = [&]
    {
      return std::make_unique<DHT>(
        id = special_id(12),
        dht::consensus_builder = instrument(3),
        storage = std::make_unique<Memory>(storage_c));
    };
  auto dht_c = make_dht_c();
  dht_b->overlay->connect(*dht_a->overlay);
  dht_c->overlay->connect(*dht_a->overlay);
  dht_c->overlay->connect(*dht_b->overlay);
  auto block = [&]
    {
      ELLE_LOG_SCOPE("write block");
      auto b = dht_a->dht->make_block<blocks::MutableBlock>(
        std::string("tombstones"));
      dht_a->dht->seal_and_insert(*b);
      return b;
    }();
  ELLE_LOG("shutdown dht C")
    dht_c.reset();
  ELLE_LOG("remove block")
    dht_a->dht->remove(block->address());
  ELLE_LOG("start dht C")
  {
    dht_c = make_dht_c();
    dht_c->overlay->connect(*dht_a->overlay);
    dht_c->overlay->connect(*dht_b->overlay);
  }
  ELLE_LOG("fetch block")
    BOOST_CHECK_THROW(dht_c->dht->fetch(block->address()),
                      memo::model::MissingBlock);
  BOOST_TEST(storage_c.size() == 0);
}

ELLE_TEST_SCHEDULED(unload)
{
  auto const max = 8;
  elle::os::setenv("MEMO_PAXOS_CACHE_SIZE", std::to_string(max));
  auto dht_a = make_dht(0);
  auto dht_b = make_dht(1);
  dht_b->overlay->connect(*dht_a->overlay);
  auto dht_c = make_dht(2);
  dht_c->overlay->connect(*dht_a->overlay);
  dht_c->overlay->connect(*dht_b->overlay);
  std::unordered_set<memo::model::Address> addresses;
  auto paxos = std::dynamic_pointer_cast<dht::consensus::Paxos::LocalPeer>(
    dht_a->dht->local());
  BOOST_REQUIRE(paxos);
  BOOST_TEST(paxos->max_addresses_size() == max);
  for (int i = 0; i < 16; ++i)
  {
    auto b = dht_a->dht->make_block<blocks::MutableBlock>(
      std::string("unload"));
    dht_a->dht->seal_and_insert(*b);
    addresses.emplace(b->address());
  }
  elle::reactor::for_each_parallel(
    {dht_a.get(), dht_b.get(), dht_c.get()},
    [&, max] (auto const& dht)
    {
      auto paxos = std::dynamic_pointer_cast<dht::consensus::Paxos::LocalPeer>(
        dht->dht->local());
      elle::reactor::for_each_parallel(
        addresses,
        [&, max] (auto const& a)
        {
          BOOST_TEST(paxos->addresses().size() <= max);
          dht->dht->fetch(a);
          BOOST_TEST(paxos->addresses().size() <= max);
        });
    });
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  auto plain = BOOST_TEST_SUITE("plain");
  suite.add(plain);
  auto paxos = BOOST_TEST_SUITE("paxos");
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
  TEST(missing_block);
  TEST(async);
  TEST(ACB);
  TEST(NB);
  TEST(UB);
  TEST(conflict);
  TEST(restart);
  TEST(cache);
  TEST(serialize);
#ifndef ELLE_WINDOWS
  TEST(monitoring);
#endif
  {
    auto remove_plain = BOOST_TEST_SUITE("removal");
    plain->add(remove_plain);
    auto remove_paxos = BOOST_TEST_SUITE("removal");
    paxos->add(remove_paxos);
    auto plain = remove_plain;
    auto paxos = remove_paxos;
    using namespace removal;
    TEST(serialize_ACB_remove);
  }
  paxos->add(BOOST_TEST_CASE(CHB_unavailable), 0, valgrind(3));
#undef TEST
  suite.add(BOOST_TEST_CASE(admin_keys), 0, valgrind(3));
  suite.add(BOOST_TEST_CASE(disabled_crypto), 0, valgrind(3));
  {
    paxos->add(ELLE_TEST_CASE(&tests_paxos::wrong_quorum, "wrong_quorum"));
    paxos->add(ELLE_TEST_CASE(&tests_paxos::batch_quorum, "batch_quorum"));
    paxos->add(ELLE_TEST_CASE(&tests_paxos::CHB_no_peer, "CHB_no_peer"));
  }
  {
    auto rebalancing = BOOST_TEST_SUITE("rebalancing");
    paxos->add(rebalancing);
    using namespace rebalancing;
    rebalancing->add(BOOST_TEST_CASE(extend_and_write), 0, valgrind(3));
    rebalancing->add(BOOST_TEST_CASE(shrink_and_write), 0, valgrind(3));
    rebalancing->add(BOOST_TEST_CASE(extend_shrink_and_write), 0, valgrind(3));
    rebalancing->add(BOOST_TEST_CASE(shrink_kill_and_write), 0, valgrind(3));
    rebalancing->add(BOOST_TEST_CASE(quorum_duel_1), 0, valgrind(3));
    rebalancing->add(BOOST_TEST_CASE(quorum_duel_2), 0, valgrind(3));
    {
      auto expand_new_CHB = [] () { expand_new_block(true); };
      auto expand_new_OKB = [] () { expand_new_block(false); };
      rebalancing->add(BOOST_TEST_CASE(expand_new_CHB), 0, valgrind(3));
      rebalancing->add(BOOST_TEST_CASE(expand_new_OKB), 0, valgrind(3));
    }
    {
      auto expand_newcomer_CHB = [] () { expand_newcomer(true); };
      auto expand_newcomer_OKB = [] () { expand_newcomer(false); };
      rebalancing->add(BOOST_TEST_CASE(expand_newcomer_CHB), 0, valgrind(3));
      rebalancing->add(BOOST_TEST_CASE(expand_newcomer_OKB), 0, valgrind(3));
    }
    rebalancing->add(BOOST_TEST_CASE(expand_concurrent), 0, valgrind(5));
    {
      auto expand_CHB_from_disk = [] () { expand_from_disk(true); };
      auto expand_OKB_from_disk = [] () { expand_from_disk(false); };
      rebalancing->add(BOOST_TEST_CASE(expand_CHB_from_disk), 0, valgrind(3));
      rebalancing->add(BOOST_TEST_CASE(expand_OKB_from_disk), 0, valgrind(3));
    }
    rebalancing->add(
      BOOST_TEST_CASE(rebalancing_while_destroyed), 0, valgrind(3));
    {
      auto evict_faulty_CHB = [] () { evict_faulty(true); };
      auto evict_faulty_OKB = [] () { evict_faulty(false); };
      rebalancing->add(BOOST_TEST_CASE(evict_faulty_CHB), 0, valgrind(3));
      rebalancing->add(BOOST_TEST_CASE(evict_faulty_OKB), 0, valgrind(3));
    }
    {
      auto evict_removed_blocks_CHB = [] () { evict_removed_blocks(true); };
      auto evict_removed_blocks_OKB = [] () { evict_removed_blocks(false); };
      rebalancing->add(BOOST_TEST_CASE(evict_removed_blocks_CHB), 0, valgrind(3));
      rebalancing->add(BOOST_TEST_CASE(evict_removed_blocks_OKB), 0, valgrind(3));
    }
    rebalancing->add(BOOST_TEST_CASE(resign), 0, valgrind(3));
    {
      auto conflict = &rebalancing::conflict;
      rebalancing->add(BOOST_TEST_CASE(conflict), 0, valgrind(3));
      auto conflict_quorum = &rebalancing::conflict_quorum;
      rebalancing->add(BOOST_TEST_CASE(conflict_quorum), 0, valgrind(3));
      auto missing_block = &rebalancing::missing_block;
      rebalancing->add(BOOST_TEST_CASE(missing_block), 0, valgrind(3));
      auto resign_insist = &rebalancing::resign_insist;
      rebalancing->add(BOOST_TEST_CASE(resign_insist), 0, valgrind(5));
      auto update_while_evicting = &rebalancing::update_while_evicting;
      rebalancing->add(BOOST_TEST_CASE(update_while_evicting), 0, valgrind(3));
      auto peeking_node = &rebalancing::peeking_node;
      rebalancing->add(BOOST_TEST_CASE(peeking_node), 0, valgrind(3));
    }
    {
      auto evict_chain = BOOST_TEST_SUITE("evict_chain");
      rebalancing->add(evict_chain);
      evict_chain->add(
        ELLE_TEST_CASE(std::bind(&rebalancing::evict_chain, true), "expand"),
        0, valgrind(5));
      evict_chain->add(
        ELLE_TEST_CASE(std::bind(&rebalancing::evict_chain, false), "rebalance"),
        0, valgrind(5));
    }
  }
  paxos->add(BOOST_TEST_CASE(tombstones), 0, valgrind(3));
  paxos->add(BOOST_TEST_CASE(unload), 0, valgrind(3));
}
