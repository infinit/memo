#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/test.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
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
  {
    this->local_a = std::make_shared<dht::Local>(
      elle::make_unique<storage::Memory>());
    this->local_b = std::make_shared<dht::Local>(
      elle::make_unique<storage::Memory>());
    dht::Passport passport_a(keys_a.K(), "network-name", keys_a.k());
    dht::Passport passport_b(keys_b.K(), "network-name", keys_a.k());
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
    local_b->doughnut() = dht_b.get();
    dht_b->overlay()->register_local(local_b);
    local_b->serve();
  }

  infinit::cryptography::rsa::KeyPair keys_a;
  infinit::cryptography::rsa::KeyPair keys_b;
  std::shared_ptr<dht::Local> local_a;
  std::shared_ptr<dht::Local> local_b;
  std::shared_ptr<dht::Doughnut> dht_a;
  std::shared_ptr<dht::Doughnut> dht_b;
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
    // No read permissions.
    // BOOST_CHECK_THROW(fetched->data(), elle::Error);
    auto acb = elle::cast<infinit::model::blocks::ACLBlock>::runtime(fetched);
    acb->data(elle::Buffer(":-(", 3));
    ELLE_LOG("other: stored edited ACB")
      // FIXME: slice
      BOOST_CHECK_THROW(dhts.dht_b->store(*acb), elle::Exception);
      // BOOST_CHECK_THROW(dhts.dht_a->store(*acb),
      //                   dht::ValidationFailed);
  }
  ELLE_LOG("owner: add ACB permissions")
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

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(doughnut));
  suite.add(BOOST_TEST_CASE(ACB));
}
