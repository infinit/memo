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

ELLE_TEST_SCHEDULED(doughnut)
{
  auto local_a = elle::make_unique<infinit::model::doughnut::Local>(
    elle::make_unique<infinit::storage::Memory>());
  auto local_b = elle::make_unique<infinit::model::doughnut::Local>(
    elle::make_unique<infinit::storage::Memory>());
  infinit::overlay::Stonehenge::Members members;
  members.push_back(local_a->server_endpoint());
  members.push_back(local_b->server_endpoint());
  infinit::model::doughnut::Doughnut dht(
    infinit::cryptography::KeyPair::generate
    (infinit::cryptography::Cryptosystem::rsa, 2048),
    elle::make_unique<infinit::overlay::Stonehenge>(std::move(members)));
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
  // Servers
  auto local_a = elle::make_unique<infinit::model::doughnut::Local>(
    elle::make_unique<infinit::storage::Memory>());
  auto local_b = elle::make_unique<infinit::model::doughnut::Local>(
    elle::make_unique<infinit::storage::Memory>());
  infinit::overlay::Stonehenge::Members members;
  {
    members.push_back(local_a->server_endpoint());
    members.push_back(local_b->server_endpoint());
  }
  local_a->doughnut().reset(
    new infinit::model::doughnut::Doughnut(
      infinit::cryptography::KeyPair::generate
      (infinit::cryptography::Cryptosystem::rsa, 2048),
      elle::make_unique<infinit::overlay::Stonehenge>(members)
      ));
  local_b->doughnut().reset(
    new infinit::model::doughnut::Doughnut(
      infinit::cryptography::KeyPair::generate
      (infinit::cryptography::Cryptosystem::rsa, 2048),
      elle::make_unique<infinit::overlay::Stonehenge>(members)
      ));
  // Clients
  infinit::model::doughnut::Doughnut dht(
    infinit::cryptography::KeyPair::generate
    (infinit::cryptography::Cryptosystem::rsa, 2048),
    elle::make_unique<infinit::overlay::Stonehenge>(members));
  auto other_keys = infinit::cryptography::KeyPair::generate
    (infinit::cryptography::Cryptosystem::rsa, 2048);
  auto other_key = other_keys.K();
  infinit::model::doughnut::Doughnut other_dht(
    std::move(other_keys),
    elle::make_unique<infinit::overlay::Stonehenge>(members));
  {
    auto block = dht.make_block<infinit::model::blocks::ACLBlock>();
    elle::Buffer data("\\_o<", 4);
    block->data(elle::Buffer(data));
    ELLE_LOG("owner: store ACB")
      dht.store(*block);
    {
      ELLE_LOG("other: fetch ACB");
      auto fetched = other_dht.fetch(block->address());
      auto acb = elle::cast<infinit::model::blocks::ACLBlock>::runtime(fetched);
      acb->data(elle::Buffer(":-(", 3));
      ELLE_LOG("other: stored edited ACB")
        // FIXME: slice
        BOOST_CHECK_THROW(other_dht.store(*acb), elle::Exception);
        // BOOST_CHECK_THROW(other_dht.store(*acb),
        //                   infinit::model::doughnut::ValidationFailed);
    }
    ELLE_LOG("owner: add ACB permissions")
      block->set_permissions(infinit::model::doughnut::User(other_key), true, true);
    ELLE_LOG("owner: store ACB")
      dht.store(*block);
    {
      ELLE_LOG("other: fetch ACB");
      auto fetched = other_dht.fetch(block->address());
      auto acb = elle::cast<infinit::model::blocks::ACLBlock>::runtime(fetched);
      acb->data(elle::Buffer(":-(", 3));
      ELLE_LOG("other: stored edited ACB")
        other_dht.store(*acb);
    }
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(doughnut));
  suite.add(BOOST_TEST_CASE(ACB));
}
