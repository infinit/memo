#define private public

#include <elle/log.hh>
#include <elle/test.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/ACB.hh> // FIXME
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
    block->data() = elle::Buffer(data);
    ELLE_LOG("store block")
      dht.store(*block);
    elle::Buffer updated(">o_/", 4);
    block->data() = elle::Buffer(updated);
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
    auto block = elle::make_unique<infinit::model::doughnut::ACB>(&dht);
    block->_doughnut = &dht;
    elle::Buffer data("\\_o<", 4);
    block->data() = elle::Buffer(data);
    ELLE_LOG("store block")
      dht.store(*block);
    elle::Buffer updated(">o_/", 4);
    block->data() = elle::Buffer(updated);
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

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(doughnut));
  suite.add(BOOST_TEST_CASE(ACB));
}
