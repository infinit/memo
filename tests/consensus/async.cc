#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/log.hh>
#include <elle/memory.hh>
#include <elle/test.hh>

#include <elle/cryptography/rsa/KeyPair.hh>

#include <elle/reactor/scheduler.hh>
#include <elle/reactor/semaphore.hh>
#include <elle/reactor/signal.hh>

#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Consensus.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Passport.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Async.test");

namespace dht = infinit::model::doughnut;

class SyncedConsensus
  : public dht::consensus::Consensus
{
public:
  SyncedConsensus(infinit::model::doughnut::Doughnut& dht)
    : dht::consensus::Consensus(dht)
  {}

  void
  _store(std::unique_ptr<infinit::model::blocks::Block>,
         infinit::model::StoreMode,
         std::unique_ptr<infinit::model::ConflictResolver>) override
  {
    while (!sem.acquire())
      elle::reactor::wait(sem);
    this->_stored.signal();
    ++nstore;
  }

  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(infinit::model::Address, boost::optional<int>) override
  {
    elle::unreachable();
  }

  void
  _remove(infinit::model::Address, infinit::model::blocks::RemoveSignature) override
  {
    while (!sem.acquire())
      elle::reactor::wait(sem);
    ++nremove;
  }

  elle::reactor::Semaphore sem;
  int nstore = 0;
  int nremove = 0;
  ELLE_ATTRIBUTE_RX(elle::reactor::Signal, stored);
};

class BlockingConsensus
  : public dht::consensus::Consensus
{
public:
  BlockingConsensus(infinit::model::doughnut::Doughnut& dht)
    : dht::consensus::Consensus(dht)
  {}

  void
  _store(std::unique_ptr<infinit::model::blocks::Block>,
         infinit::model::StoreMode,
         std::unique_ptr<infinit::model::ConflictResolver>) override
  {
    elle::reactor::sleep();
    elle::unreachable();
  }

  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(infinit::model::Address, boost::optional<int>) override
  {
    elle::reactor::sleep();
    elle::unreachable();
  }

  void
  _remove(infinit::model::Address, infinit::model::blocks::RemoveSignature) override
  {
    elle::reactor::sleep();
    elle::unreachable();
  }
};

class DummyDoughnut
  : public dht::Doughnut
{
public:
  DummyDoughnut()
    : DummyDoughnut(infinit::model::Address::random(0), // FIXME
                    elle::cryptography::rsa::keypair::generate(1024))
  {}

  DummyDoughnut(infinit::model::Address id,
                elle::cryptography::rsa::KeyPair keys)
    : dht::Doughnut(
      id,
      std::make_shared<elle::cryptography::rsa::KeyPair>(keys),
      keys.public_key(),
      infinit::model::doughnut::Passport(keys.K(), "network", keys),
      [] (dht::Doughnut&) { return nullptr; },
      [] (dht::Doughnut&, std::shared_ptr<dht::Local>) { return nullptr; })
  {}
};

// Check blocks are fetched from the queue, even if stored on disk
ELLE_TEST_SCHEDULED(fetch_disk_queued)
{
  auto const d = elle::filesystem::TemporaryDirectory{};
  auto const a1 = infinit::model::Address::random(0); // FIXME
  auto const a2 = infinit::model::Address::random(0); // FIXME
  auto const keys = elle::cryptography::rsa::keypair::generate(1024);
  infinit::model::doughnut::Passport passport(keys.K(), "network", keys);
  DummyDoughnut dht;
  {
    auto&& async = dht::consensus::Async(
      std::make_unique<BlockingConsensus>(dht), d.path(), 1);
    async.store(std::make_unique<infinit::model::blocks::Block>(
                  a1, elle::Buffer("a1", 2)),
                infinit::model::STORE_INSERT, nullptr);
    async.store(std::make_unique<infinit::model::blocks::Block>(
                  a2, elle::Buffer("a2", 2)),
                infinit::model::STORE_INSERT, nullptr);
    BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a1");
    BOOST_CHECK_EQUAL(async.fetch(a2)->data(), "a2");
  }
  ELLE_LOG("reload from disk")
  {
    auto&& async = dht::consensus::Async(
      std::make_unique<BlockingConsensus>(dht), d.path(), 1);
    BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a1");
    BOOST_CHECK_EQUAL(async.fetch(a2)->data(), "a2");
  }
}

ELLE_TEST_SCHEDULED(fetch_disk_queued_multiple)
{
  DummyDoughnut dht;
  auto d =  elle::filesystem::TemporaryDirectory{};
  auto const a1 = infinit::model::Address::random(0); // FIXME
  {
    auto scu = std::make_unique<SyncedConsensus>(dht);
    auto& sc = *scu;
    auto&& async = dht::consensus::Async(std::move(scu), d.path(), 1);
    ELLE_LOG("insert block")
      async.store(std::make_unique<infinit::model::blocks::Block>(
                    a1, elle::Buffer("a1", 2)),
                  infinit::model::STORE_INSERT, nullptr);
    ELLE_LOG("update block")
      async.store(std::make_unique<infinit::model::blocks::Block>(
                    a1, elle::Buffer("a2", 2)),
                  infinit::model::STORE_UPDATE, nullptr);
    ELLE_LOG("update block")
      async.store(std::make_unique<infinit::model::blocks::Block>(
                    a1, elle::Buffer("a3", 2)),
                  infinit::model::STORE_UPDATE, nullptr);
    ELLE_LOG("fetch block")
      BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a3");
    sc.sem.release();
    elle::reactor::wait(sc.stored());
    ELLE_LOG("fetch block")
      BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a3");
    sc.sem.release();
    elle::reactor::wait(sc.stored());
    ELLE_LOG("fetch block")
      BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a3");
    sc.sem.release();
    elle::reactor::wait(sc.stored());
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(fetch_disk_queued), 0, 10);
  suite.add(BOOST_TEST_CASE(fetch_disk_queued_multiple), 0, 10);
}
