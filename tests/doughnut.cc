#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/test.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/storage/Memory.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.test");

namespace dht = infinit::model::doughnut;
namespace storage = infinit::storage;

class DHTs
{
public:
  DHTs()
    : keys_a(infinit::cryptography::rsa::keypair::generate(2048))
    , keys_b(infinit::cryptography::rsa::keypair::generate(2048))
    , keys_c(infinit::cryptography::rsa::keypair::generate(2048))
  {
    this->local_a = std::make_shared<dht::Local>(
      elle::make_unique<storage::Memory>());
    this->local_b = std::make_shared<dht::Local>(
      elle::make_unique<storage::Memory>());
    this->local_c = std::make_shared<dht::Local>(
      elle::make_unique<storage::Memory>());
    dht::Passport passport_a(keys_a.K(), "network-name", keys_a.k());
    dht::Passport passport_b(keys_b.K(), "network-name", keys_a.k());
    dht::Passport passport_c(keys_c.K(), "network-name", keys_a.k());
    infinit::overlay::Stonehenge::Hosts members;
    members.push_back(local_a->server_endpoint());
    members.push_back(local_b->server_endpoint());
    this->dht_a = std::make_shared<dht::Doughnut>(
      keys_a,
      keys_a.K(),
      passport_a,
      static_cast<infinit::model::doughnut::Doughnut::OverlayBuilder>(
        [=](infinit::model::doughnut::Doughnut*d) {
          return elle::make_unique<infinit::overlay::Stonehenge>(
            elle::UUID::random(), members, d);
        }),
      boost::filesystem::path(".")
      );
    local_a->doughnut() = dht_a.get();
    dht_a->overlay()->register_local(local_a);
    local_a->serve();
    this->dht_b = std::make_shared<dht::Doughnut>(
      keys_b,
      keys_a.K(),
      passport_b,
      static_cast<infinit::model::doughnut::Doughnut::OverlayBuilder>(
        [=](infinit::model::doughnut::Doughnut*d) {
          return elle::make_unique<infinit::overlay::Stonehenge>(
            elle::UUID::random(), members, d);
        }),
      boost::filesystem::path(".")
      );
    this->dht_c = std::make_shared<dht::Doughnut>(
      keys_c,
      keys_a.K(),
      passport_c,
      static_cast<infinit::model::doughnut::Doughnut::OverlayBuilder>(
        [=](infinit::model::doughnut::Doughnut*d) {
          return elle::make_unique<infinit::overlay::Stonehenge>(
            elle::UUID::random(), members, d);
        }),
      boost::filesystem::path("."),
      nullptr,
      1,
      true
      );
    local_a->doughnut() = dht_a.get();
    dht_a->overlay()->register_local(local_a);
    local_a->serve();
    local_b->doughnut() = dht_b.get();
    dht_b->overlay()->register_local(local_b);
    local_b->serve();
    local_c->doughnut() = dht_c.get();
    dht_c->overlay()->register_local(local_c);
    local_c->serve();
  }

  infinit::cryptography::rsa::KeyPair keys_a;
  infinit::cryptography::rsa::KeyPair keys_b;
  infinit::cryptography::rsa::KeyPair keys_c;
  std::shared_ptr<dht::Local> local_a;
  std::shared_ptr<dht::Local> local_b;
  std::shared_ptr<dht::Local> local_c;
  std::shared_ptr<dht::Doughnut> dht_a;
  std::shared_ptr<dht::Doughnut> dht_b;
  std::shared_ptr<dht::Doughnut> dht_c;
};

ELLE_TEST_SCHEDULED(doughnut)
{
  DHTs dhts;
  auto& dht = *dhts.dht_a;
  {
    elle::Buffer data("\\_o<", 4);
    auto block = dht.make_block<infinit::model::blocks::ImmutableBlock>(data);
    ELLE_LOG("store block")
      dht.store(*block);
    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
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

ELLE_TEST_SCHEDULED(async)
{
  DHTs dhts;
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
    for (auto const& block: blocks_)
      dht.store(*block);

    ELLE_LOG("fetch block")
      ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
    for (auto const& block: blocks_)
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

ELLE_TEST_SCHEDULED(ACB)
{
  DHTs dhts;
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
    block->set_permissions(dht::User(dhts.keys_b.K(), ""), true, false);
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
    block->set_permissions(dht::User(dhts.keys_b.K(), ""), true, true);
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

ELLE_TEST_SCHEDULED(NB)
{
  DHTs dhts;
  auto block = elle::make_unique<dht::NB>(
    dhts.dht_a.get(), dhts.keys_a.K(), "blockname",
    elle::Buffer("blockdata", 9));
  ELLE_LOG("owner: store NB")
    dhts.dht_a->store(*block);
  {
    ELLE_LOG("other: fetch NB");
    auto fetched =
      dhts.dht_b->fetch(dht::NB::address(dhts.keys_a.K(), "blockname"));
    BOOST_CHECK_EQUAL(fetched->data(), "blockdata");
    auto nb = elle::cast<dht::NB>::runtime(fetched);
    BOOST_CHECK(nb);
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(doughnut));
  suite.add(BOOST_TEST_CASE(async));
  suite.add(BOOST_TEST_CASE(ACB));
  suite.add(BOOST_TEST_CASE(NB));
}
