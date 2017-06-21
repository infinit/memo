#include <elle/test.hh>

#include "../DHT.hh"

ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Paxos.test");

ELLE_TEST_SCHEDULED(availability_2)
{
  auto a = std::make_unique<DHT>();
  auto b = std::make_unique<DHT>();
  a->overlay->connect(*b->overlay);
  auto block = a->dht->make_block<memo::model::blocks::MutableBlock>();
  block->data(elle::Buffer("foo"));
  ELLE_LOG("insert block")
    a->dht->seal_and_insert(*block);
  block->data(elle::Buffer("foobar"));
  ELLE_LOG("update block")
    a->dht->seal_and_update(*block);
  ELLE_LOG("stop second DHT")
    b.reset();
  ELLE_LOG("read block")
    BOOST_CHECK_EQUAL(a->dht->fetch(block->address())->data(), "foobar");
  ELLE_LOG("update block")
  {
    block->data(elle::Buffer("foobarbaz"));
    BOOST_CHECK_THROW(a->dht->seal_and_update(*block),
                      elle::athena::paxos::TooFewPeers);
  }
}

ELLE_TEST_SCHEDULED(availability_3)
{
  auto a = std::make_unique<DHT>();
  auto b = std::make_unique<DHT>();
  auto c = std::make_unique<DHT>();
  a->overlay->connect(*b->overlay);
  a->overlay->connect(*c->overlay);
  b->overlay->connect(*c->overlay);
  auto block = a->dht->make_block<memo::model::blocks::MutableBlock>();
  ELLE_LOG("store block")
  {
    block->data(elle::Buffer("foo"));
    a->dht->seal_and_insert(*block);
    block->data(elle::Buffer("foobar"));
    a->dht->seal_and_update(*block);
  }
  ELLE_LOG("test 2/3 nodes")
  {
    c.reset();
    BOOST_CHECK_EQUAL(b->dht->fetch(block->address())->data(), "foobar");
    block->data(elle::Buffer("foobarbaz"));
    a->dht->seal_and_update(*block);
  }
  ELLE_LOG("test 1/3 nodes")
  {
    b.reset();
    BOOST_CHECK_THROW(a->dht->fetch(block->address())->data(),
                      elle::athena::paxos::TooFewPeers);
    block->data(elle::Buffer("foobarbazquux"));
    BOOST_CHECK_THROW(a->dht->seal_and_update(*block),
                      elle::athena::paxos::TooFewPeers);
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(availability_2), 0, 10);
  suite.add(BOOST_TEST_CASE(availability_3), 0, 10);
}
