#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/log.hh>
#include <elle/memory.hh>
#include <elle/test.hh>

#include <reactor/scheduler.hh>
#include <reactor/semaphore.hh>
#include <reactor/signal.hh>

#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Consensus.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Async.test");

namespace dht = infinit::model::doughnut;

class SyncedConsensus
  : public dht::consensus::Consensus
{
public:
  reactor::Semaphore sem;
  int nstore, nremove;
  SyncedConsensus()
    : dht::consensus::Consensus(*(dht::Doughnut*)(nullptr))
    , nstore(0)
    , nremove(0)
  {}

  virtual
  void
  _store(std::unique_ptr<infinit::model::blocks::Block>,
         infinit::model::StoreMode,
         std::unique_ptr<infinit::model::ConflictResolver>) override
  {
    reactor::wait(sem);
    this->_stored.signal();
    ++nstore;
  }

  virtual
  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(infinit::model::Address, boost::optional<int>) override
  {
    elle::unreachable();
  }

  virtual
  void
  _remove(infinit::model::Address) override
  {
    reactor::wait(sem);
    ++nremove;
  }

  ELLE_ATTRIBUTE_RX(reactor::Signal, stored);
};

class BlockingConsensus
  : public dht::consensus::Consensus
{
public:
  BlockingConsensus()
    : dht::consensus::Consensus(*(dht::Doughnut*)(nullptr))
  {}

  virtual
  void
  _store(std::unique_ptr<infinit::model::blocks::Block>,
         infinit::model::StoreMode,
         std::unique_ptr<infinit::model::ConflictResolver>) override
  {
    reactor::sleep();
    elle::unreachable();
  }

  virtual
  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(infinit::model::Address, boost::optional<int>) override
  {
    reactor::sleep();
    elle::unreachable();
  }

  virtual
  void
  _remove(infinit::model::Address) override
  {
    reactor::sleep();
    elle::unreachable();
  }
};

// Check blocks are fetched from the queue, even if stored on disk
ELLE_TEST_SCHEDULED(fetch_disk_queued)
{
  elle::filesystem::TemporaryDirectory d;
  auto a1 = infinit::model::Address::random();
  auto a2 = infinit::model::Address::random();
  {
    dht::consensus::Async async(
      elle::make_unique<BlockingConsensus>(), d.path(), 1);
    async.store(elle::make_unique<infinit::model::blocks::Block>(
                  a1, elle::Buffer("a1", 2)),
                infinit::model::STORE_INSERT, nullptr);
    async.store(elle::make_unique<infinit::model::blocks::Block>(
                  a2, elle::Buffer("a2", 2)),
                infinit::model::STORE_INSERT, nullptr);
    BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a1");
    BOOST_CHECK_EQUAL(async.fetch(a2)->data(), "a2");
  }
  ELLE_LOG("reload from disk")
  {
    dht::consensus::Async async(
      elle::make_unique<BlockingConsensus>(), d.path(), 1);
    BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a1");
    BOOST_CHECK_EQUAL(async.fetch(a2)->data(), "a2");
  }
}

ELLE_TEST_SCHEDULED(fetch_disk_queued_multiple)
{
  elle::filesystem::TemporaryDirectory d;
  auto a1 = infinit::model::Address::random();
  {
    auto scu = elle::make_unique<SyncedConsensus>();
    auto& sc = *scu;
    dht::consensus::Async async(std::move(scu), d.path(), 1);
    async.store(elle::make_unique<infinit::model::blocks::Block>(
                  a1, elle::Buffer("a1", 2)),
                infinit::model::STORE_INSERT, nullptr);
    async.store(elle::make_unique<infinit::model::blocks::Block>(
                  a1, elle::Buffer("a2", 2)),
                infinit::model::STORE_UPDATE, nullptr);
    async.store(elle::make_unique<infinit::model::blocks::Block>(
                  a1, elle::Buffer("a3", 2)),
                infinit::model::STORE_UPDATE, nullptr);
    BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a3");
    sc.sem.release();
    reactor::wait(sc.stored());
    BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a3");
    sc.sem.release();
    reactor::wait(sc.stored());
    BOOST_CHECK_EQUAL(async.fetch(a1)->data(), "a3");
    sc.sem.release();
    reactor::wait(sc.stored());
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(fetch_disk_queued), 0, 5);
  suite.add(BOOST_TEST_CASE(fetch_disk_queued_multiple), 0, 5);
}
