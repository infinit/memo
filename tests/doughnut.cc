#include <elle/log.hh>
#include <elle/test.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/storage/Memory.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.test");

ELLE_TEST_SCHEDULED(doughnut)
{
  auto local_a = elle::make_unique<infinit::model::doughnut::Local>(
    elle::make_unique<infinit::storage::Memory>());
  auto local_b = elle::make_unique<infinit::model::doughnut::Local>(
    elle::make_unique<infinit::storage::Memory>());

  std::vector<std::unique_ptr<infinit::model::doughnut::Peer>> peers;
  auto endpoint = local_a->server_endpoint();
  peers.push_back(std::move(local_a));
  peers.push_back(elle::make_unique<infinit::model::doughnut::Remote>
                  ("127.0.0.1", endpoint.port()));
  infinit::model::doughnut::Doughnut dht(std::move(peers));

  auto block = dht.make_block();
  elle::Buffer data("\\_o<", 4);
  block->data() = elle::Buffer(data);
  ELLE_LOG("store block")
    dht.store(*block);
  ELLE_LOG("fetch block")
    ELLE_ASSERT_EQ(dht.fetch(block->address())->data(), data);
  ELLE_LOG("remove block")
    dht.remove(block->address());
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(doughnut));
}
