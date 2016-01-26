#include <elle/test.hh>
#include <elle/json/json.hh>
#include <elle/serialization/json.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Storage.hh>


ELLE_LOG_COMPONENT("test");

namespace ifs = infinit::filesystem;
namespace rfs = reactor::filesystem;
namespace bfs = boost::filesystem;
namespace imd = infinit::model::doughnut;
namespace iok = infinit::overlay::kelips;

static std::vector<std::shared_ptr<imd::Doughnut>>
run_nodes(bfs::path where,  infinit::cryptography::rsa::KeyPair& kp,
          int count = 10, int groups = 1, int replication_factor = 3)
{
  ELLE_LOG("building and running %s nodes", count);
  std::vector<std::shared_ptr<imd::Doughnut>> res;
  iok::Configuration config;
  config.k = groups;
  config.encrypt = true;
  config.accept_plain = false;
  config.contact_timeout_ms = 1000;
  config.ping_interval_ms = 1000 / count / 3;
  config.ping_timeout_ms = 200;
  infinit::overlay::NodeEndpoints endpoints;
  for (int n=0; n<count; ++n)
  {
    std::unique_ptr<infinit::storage::Storage> s;
    boost::filesystem::create_directories(where / "store");
    s.reset(new infinit::storage::Filesystem(where / ("store" + std::to_string(n))));
    infinit::model::doughnut::Passport passport(kp.K(), "testnet", kp);
    infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
    [&] (infinit::model::doughnut::Doughnut& dht)
        -> std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
        {
          return elle::make_unique<imd::consensus::Paxos>(dht, replication_factor);
        };
    infinit::model::doughnut::Doughnut::OverlayBuilder overlay =
        [&] (infinit::model::doughnut::Doughnut& dht,
             infinit::model::Address id,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          return config.make(id, endpoints, local, &dht);
        };
    infinit::model::Address::Value v;
    memset(v, 0, sizeof(v));
    v[0] = n+1;
    auto dn = std::make_shared<infinit::model::doughnut::Doughnut>(
      infinit::model::Address(v),
      std::make_shared<infinit::cryptography::rsa::KeyPair>(kp),
      kp.public_key(),
      passport,
      consensus,
      overlay,
      boost::optional<int>(),
      std::move(s));
    res.push_back(dn);
    //if (res.size() == 1)
    {
      std::string ep = "127.0.0.1:"
        + std::to_string(dn->local()->server_endpoint().port());
      std::vector<std::string> eps;
      eps.push_back(ep);
      endpoints.emplace(dn->id(), eps);
    }
    reactor::sleep(500_ms);
  }
  return res;
}

static std::unique_ptr<rfs::FileSystem>
make_observer(std::shared_ptr<imd::Doughnut>& root_node,
              bfs::path where,
              infinit::cryptography::rsa::KeyPair& kp,
              int groups,
              int replication_factor,
              bool cache, bool async)
{
  ELLE_LOG("building observer");
  iok::Configuration config;
  config.k = groups;
  config.encrypt = true;
  config.accept_plain = false;
  infinit::overlay::NodeEndpoints endpoints;
  std::string ep = "127.0.0.1:"
    + std::to_string(root_node->local()->server_endpoint().port());
  std::vector<std::string> eps;
  eps.push_back(ep);
  endpoints.emplace(root_node->id(), eps);
  infinit::model::doughnut::Passport passport(kp.K(), "testnet", kp);
  infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
  [&] (infinit::model::doughnut::Doughnut& dht)
  -> std::unique_ptr<imd::consensus::Consensus>
  {
    std::unique_ptr<imd::consensus::Consensus> backend = 
      elle::make_unique<imd::consensus::Paxos>(dht, replication_factor);
    if (async)
    {
      auto async = elle::make_unique<
        infinit::model::doughnut::consensus::Async>(std::move(backend),
          where / boost::filesystem::unique_path());
        backend = std::move(async);
    }
    if (cache)
    {
      auto cache = elle::make_unique<imd::consensus::Cache>(
        std::move(backend));
      backend = std::move(cache);
    }
    return std::move(backend);
  };
  infinit::model::doughnut::Doughnut::OverlayBuilder overlay =
  [&] (infinit::model::doughnut::Doughnut& dht,
    infinit::model::Address id,
    std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return config.make(id, endpoints, local, &dht);
  };
  auto dn = std::make_shared<infinit::model::doughnut::Doughnut>(
    infinit::model::Address::random(),
    std::make_shared<infinit::cryptography::rsa::KeyPair>(kp),
    kp.public_key(),
    passport,
    consensus,
    overlay,
    boost::optional<int>(),
    std::unique_ptr<infinit::storage::Storage>());
  auto ops = elle::make_unique<infinit::filesystem::FileSystem>("volume", dn);
  auto fs = elle::make_unique<reactor::filesystem::FileSystem>(std::move(ops), true);
  reactor::sleep(500_ms);
  ELLE_LOG("Returning observer");
  return fs;
}

void writefile(rfs::FileSystem& fs,
               std::string const& name,
               std::string const& content)
{
  auto handle = fs.path("/")->child(name)->create(O_RDWR, 0666 | S_IFREG);
  handle->write(elle::WeakBuffer((char*)content.data(), content.size()),
                content.size(), 0);
  handle->close();
  handle.reset();
}

void appendfile(rfs::FileSystem& fs,
               std::string const& name,
               std::string const& content)
{
  auto handle = fs.path("/")->child(name)->open(O_RDWR, 0666 | S_IFREG);
  struct stat st;
  fs.path("/")->child(name)->stat(&st);
  handle->write(elle::WeakBuffer((char*)content.data(), content.size()),
                content.size(), st.st_size);
  handle->close();
  handle.reset();
}

std::string readfile(rfs::FileSystem& fs,
              std::string const& name)
{
  auto handle = fs.path("/")->child(name)->open(O_RDONLY, S_IFREG);
  std::string res(32768, 0);
  int sz = handle->read(elle::WeakBuffer(elle::unconst(res.data()), 32768), 32768, 0);
  res.resize(sz);
  ELLE_TRACE("got %s bytes", sz);
  return res;
}

ELLE_TEST_SCHEDULED(basic)
{
  auto tmp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  auto kp = infinit::cryptography::rsa::keypair::generate(2048);
  auto nodes = run_nodes(tmp, kp);
  auto fs = make_observer(nodes.front(), tmp, kp, 1, 3, false, false);
  writefile(*fs, "test1", "foo");
  writefile(*fs, "test2", std::string(32000, 'a'));
  BOOST_CHECK_EQUAL(readfile(*fs, "test1"), "foo");
  BOOST_CHECK_EQUAL(readfile(*fs, "test2"), std::string(32000, 'a'));
  // reset all
  ELLE_LOG("teardown");
  fs.reset();
  nodes.clear();
  nodes = run_nodes(tmp, kp);
  fs = make_observer(nodes.front(), tmp, kp, 1, 3, false, false);
  BOOST_CHECK_EQUAL(readfile(*fs, "test1"), "foo");
  BOOST_CHECK_EQUAL(readfile(*fs, "test2"), std::string(32000, 'a'));
  ELLE_LOG("teardown");
}

void test_kill_nodes(int node_count, int replication, int kill, bool overwrite_while_down)
{
  static const int file_count = 20;
  ELLE_LOG("kill_nodes n=%s k=%s r=%s", node_count, kill, replication);
  auto tmp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  auto kp = infinit::cryptography::rsa::keypair::generate(2048);
  auto nodes = run_nodes(tmp, kp, node_count, 1, replication);
  auto fs = make_observer(nodes.front(), tmp, kp, 1, replication, false, false);
  ELLE_LOG("initial file write");
  for (int i=0; i<file_count; ++i)
    writefile(*fs, "foo" + std::to_string(i), "foo");
  for (int i=0; i<file_count; ++i)
    BOOST_CHECK_EQUAL(readfile(*fs, "foo" + std::to_string(i)), "foo");
  ELLE_LOG("killing nodes");
  for (int k=0; k<kill; ++k)
  {
    // We must kill the same nodes as the ones we wont reinstantiate below
    int idx = nodes.size() - k - 1;
    nodes[idx].reset();
  }
  ELLE_LOG("checking files");
  for (int i=0; i<file_count; ++i)
  {
    std::cerr << "\r" << i << "                ";
    BOOST_CHECK_EQUAL(readfile(*fs, "foo" + std::to_string(i)), "foo");
  }

  ELLE_LOG("write new files");
  // WRITE
  for (int i=0; i<file_count; ++i)
  {
    std::cerr << "\r" << i << "                ";
    writefile(*fs, "bar" + std::to_string(i), "bar");
  }
  if (overwrite_while_down)
  {
    ELLE_LOG("append existing files");
    for (int i=0; i<file_count; ++i)
    {
      std::cerr << "\r" << i << "                ";
      appendfile(*fs, "foo" + std::to_string(i), "foo");
    }
  }
  // OVERWRITE
  ELLE_LOG("teardown");
  fs.reset();
  nodes.clear();
  ELLE_LOG("reinitialize");
  nodes = run_nodes(tmp, kp, node_count - kill, 1, replication); // dont reload the last node
  fs = make_observer(nodes.front(), tmp, kp, 1, replication, false, false);
  if (overwrite_while_down)
  {
    ELLE_LOG("overwrite");
    for (int i=0; i<file_count; ++i)
    {
      std::cerr << "\r" << i << "                ";
      appendfile(*fs, "foo" + std::to_string(i), "foo");
    }
  }
  ELLE_LOG("check");
  auto value = overwrite_while_down ? "foofoofoo" : "foo";
  for (int i=0; i<file_count; ++i)
    BOOST_CHECK_EQUAL(readfile(*fs, "foo" + std::to_string(i)), value);
  for (int i=0; i<file_count; ++i)
    BOOST_CHECK_EQUAL(readfile(*fs, "bar" + std::to_string(i)), "bar");
  ELLE_LOG("final teardown");
}

ELLE_TEST_SCHEDULED(killed_nodes)
{
  test_kill_nodes(5, 3, 1, true);
}
ELLE_TEST_SCHEDULED(killed_nodes_big)
{
  test_kill_nodes(10, 5, 2, true);
}
ELLE_TEST_SCHEDULED(killed_nodes_more)
{
  test_kill_nodes(5, 3, 2, false);
}
ELLE_TEST_SCHEDULED(killed_nodes_half)
{
  test_kill_nodes(5, 2, 1, false);
}

ELLE_TEST_SUITE()
{
  srand(time(nullptr));
  setenv("INFINIT_CONNECT_TIMEOUT", "1", 1);
  setenv("INFINIT_SOFTFAIL_TIMEOUT", "2", 1);
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(basic), 0, 120);
  suite.add(BOOST_TEST_CASE(killed_nodes), 0, 600);
  suite.add(BOOST_TEST_CASE(killed_nodes_big), 0, 600);
  // fails
  //suite.add(BOOST_TEST_CASE(killed_nodes_half), 0, 600);
  // fails, read requires a consensus
  //suite.add(BOOST_TEST_CASE(killed_nodes_more), 0, 600);
}