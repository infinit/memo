#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/make-vector.hh>
#include <elle/os/environ.hh>
#include <elle/reactor/network/http-server.hh>
#include <elle/test.hh>

#include <elle/reactor/for-each.hh>

#include <infinit/Network.hh>
#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/Endpoints.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/silo/Filesystem.hh>
#include <infinit/silo/Memory.hh>
#include <infinit/silo/Storage.hh>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>

ELLE_LOG_COMPONENT("test");

#ifdef INFINIT_WINDOWS
# undef stat
#endif

namespace ifs = infinit::filesystem;
namespace rfs = elle::reactor::filesystem;
namespace bfs = boost::filesystem;
namespace imd = infinit::model::doughnut;
namespace iok = infinit::overlay::kelips;
using infinit::model::Endpoints;

namespace
{
  template <typename Fun>
  auto insist(Fun fun, int attempts = 10)
  {
    for (int i=0; ; ++i)
    {
      try
      {
        ELLE_LOG("insist: attempt %s/%s", i, attempts);
        return fun();
      }
      catch (elle::Error const& e)
      {
        ELLE_LOG("insist: caught %s", e);
        if (attempts <= i)
          throw;
      }
      catch (...)
      {
        ELLE_ERR("insist: caught unexpected %s", elle::exception_string());
        throw;
      }
      elle::reactor::sleep(1_sec);
    }
    elle::unreachable();
  }
}

struct BEndpoints
{
  std::vector<std::string> addresses;
  int port;
  using Model = elle::das::Model<
    BEndpoints,
    decltype(elle::meta::list(infinit::symbols::addresses,
                              infinit::symbols::port))>;
};
ELLE_DAS_SERIALIZE(BEndpoints);

inline
BEndpoints
e2b(Endpoints const& eps)
{
  auto res = BEndpoints{};
  res.port = eps.begin()->port();
  for (auto const& a: eps)
    res.addresses.push_back(a.address().to_string());
  return res;
}


class Beyond: public elle::reactor::network::HttpServer
{
public:
  Beyond()
  {
    register_route("/networks/bob/network/endpoints", elle::reactor::http::Method::GET,
      [&] (
           Headers const&,
           Cookies const&,
           Parameters const&,
           elle::Buffer const&) -> std::string
      {
        auto res = elle::serialization::json::serialize(_endpoints, false).string();
        while (true)
        {
          auto p = res.find("null");
          if (p == res.npos)
            break;
          res = res.substr(0, p) + "{}" + res.substr(p+4);
        }
        ELLE_LOG("endpoints: '%s'", res);
        return res;
      });
    elle::os::setenv("INFINIT_BEYOND", elle::sprintf("127.0.0.1:%s", this->port()), true);
  }
  ~Beyond()
  {
  }
  void push(infinit::model::Address id, Endpoints eps)
  {
    _endpoints["bob"][elle::sprintf("%s", id)] = e2b(eps);
  }
  void pull(infinit::model::Address id)
  {
    _endpoints["bob"].erase(elle::sprintf("%s", id));
  }
  void push(infinit::model::doughnut::Doughnut& d)
  {
    Endpoints eps {
      {
        boost::asio::ip::address::from_string("127.0.0.1"),
        d.local()->server_endpoint().port(),
      },
    };
    push(d.id(), eps);
  }
  void pull(infinit::model::doughnut::Doughnut& d)
  {
    pull(d.id());
  }
  void cleanup()
  {
    _endpoints.clear();
  }
private:
  // username -> {node_id -> endpoints}
  using NetEndpoints = std::unordered_map<std::string,
    std::unordered_map<std::string, BEndpoints>>;
  NetEndpoints _endpoints;
};

auto endpoints = std::vector<infinit::model::Endpoints>{};
auto net = infinit::Network("bob/network", nullptr, boost::none);
using Nodes = std::vector<
         std::pair<
           std::shared_ptr<imd::Doughnut>,
           elle::reactor::Thread::unique_ptr
           >>;

/// \param count   number of nodes to create.
static
Nodes
run_nodes(bfs::path where,
          elle::cryptography::rsa::KeyPair const& kp,
          int count = 10, int groups = 1, int replication_factor = 3,
          bool paxos_lenient = false,
          int beyond_port = 0, int base_id=0)
{
  ELLE_LOG_SCOPE("building and running %s nodes", count);
  endpoints.clear();
  Nodes res;
  iok::Configuration config;
  config.k = groups;
  config.encrypt = true;
  config.accept_plain = false;
  int factor =
#ifdef INFINIT_WINDOWS
    5;
#else
    1;
#endif
  config.contact_timeout_ms = factor * valgrind(2000,20);
  config.ping_interval_ms = factor * valgrind(1000, 10) / count / 3;
  config.ping_timeout_ms = factor * valgrind(500, 20);
  config.query_get_retries = 2;
  for (int n=0; n<count; ++n)
  {
    std::unique_ptr<infinit::storage::Storage> s;
    bfs::create_directories(where / "store");
    s.reset(new infinit::storage::Filesystem(where / ("store" + std::to_string(n))));
    auto passport = infinit::model::doughnut::Passport(kp.K(), "testnet", kp);
    auto consensus =
    [&] (infinit::model::doughnut::Doughnut& dht)
        -> std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
        {
          return std::make_unique<imd::consensus::Paxos>(dht, replication_factor, paxos_lenient);
        };
    auto overlay =
        [&] (infinit::model::doughnut::Doughnut& dht,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          auto res = config.make(local, &dht);
          res->discover(endpoints);
          return res;
        };
    infinit::model::Address::Value v;
    memset(v, 0, sizeof(v));
    v[0] = base_id + n + 1;
    auto dn = std::make_shared<infinit::model::doughnut::Doughnut>(
      infinit::model::Address(v),
      std::make_shared<elle::cryptography::rsa::KeyPair>(kp),
      kp.public_key(),
      passport,
      consensus,
      overlay,
      boost::optional<int>(),
      boost::optional<boost::asio::ip::address>(),
      std::move(s));
    elle::reactor::Thread::unique_ptr tptr;
    if (beyond_port)
    {
      infinit::overlay::NodeLocations locs;
      tptr = net.make_poll_beyond_thread(*dn, locs, 1);
    }
    res.emplace_back(dn, std::move(tptr));
    //if (res.size() == 1)
    endpoints.emplace_back(
        infinit::model::Endpoints{{
            boost::asio::ip::address::from_string("127.0.0.1"),
            dn->local()->server_endpoint().port(),
        }});
  }
  return res;
}

static
std::pair<std::unique_ptr<rfs::FileSystem>, elle::reactor::Thread::unique_ptr>
make_observer(std::shared_ptr<imd::Doughnut>& root_node,
              bfs::path where,
              elle::cryptography::rsa::KeyPair const& kp,
              int groups,
              int replication_factor,
              bool cache, bool async,
              bool paxos_lenient,
              int beyond_port = 0)
{
  ELLE_LOG_SCOPE("building observer");
  iok::Configuration config;
  config.k = groups;
  config.encrypt = true;
  config.accept_plain = false;
  config.query_get_retries = 3;
  config.ping_timeout_ms = valgrind(500, 20);
  auto passport = infinit::model::doughnut::Passport(kp.K(), "testnet", kp);
  auto consensus =
    [&] (infinit::model::doughnut::Doughnut& dht)
    {
      auto backend =
        std::unique_ptr<imd::consensus::Consensus>{
          std::make_unique<imd::consensus::Paxos>(dht, replication_factor, paxos_lenient)
        };
      if (async)
        backend = std::make_unique<
          infinit::model::doughnut::consensus::Async>(std::move(backend),
            where / bfs::unique_path());
      if (cache)
        backend = std::make_unique<imd::consensus::Cache>(
          std::move(backend));
      return backend;
    };
  auto overlay =
    [&] (infinit::model::doughnut::Doughnut& dht,
         std::shared_ptr<infinit::model::doughnut::Local> local)
    {
      auto res = config.make(local, &dht);
      res->discover(endpoints);
      return res;
    };
  auto dn = std::make_shared<infinit::model::doughnut::Doughnut>(
    infinit::model::Address::random(0), // FIXME
    std::make_shared<elle::cryptography::rsa::KeyPair>(kp),
    kp.public_key(),
    passport,
    consensus,
    overlay,
    boost::optional<int>(),
    boost::optional<boost::asio::ip::address>(),
    std::unique_ptr<infinit::storage::Storage>());
  auto ops = std::make_unique<ifs::FileSystem>(
    "volume", dn, ifs::allow_root_creation = true);
  auto fs = std::make_unique<elle::reactor::filesystem::FileSystem>(
    std::move(ops), true);
  elle::reactor::Thread::unique_ptr tptr;
  if (beyond_port)
  {
    infinit::overlay::NodeLocations locs;
    tptr = net.make_poll_beyond_thread(*dn, locs, 1);
  }
  while (true)
  {
    auto stats = dn->overlay()->query("stats", {});
    auto ostats = boost::any_cast<elle::json::Object>(stats);
    auto cts = boost::any_cast<elle::json::Array>(ostats["contacts"]);
    if (!cts.empty())
    {
      auto c = boost::any_cast<elle::json::Object>(cts.front());
      if (boost::any_cast<bool>(c.at("discovered")))
        break;
    }
    elle::reactor::sleep(50_ms);
  }
  ELLE_LOG("Returning observer");
  return std::make_pair(std::move(fs), std::move(tptr));
}

namespace
{
  std::vector<std::unique_ptr<rfs::FileSystem>>
  node_to_fs(Nodes const& nodes)
  {
    return elle::make_vector(nodes, [](auto const& n) {
        auto ops = std::make_unique<ifs::FileSystem>(
          "volume", n.first, ifs::allow_root_creation = true);
        return std::make_unique<elle::reactor::filesystem::FileSystem>(
          std::move(ops), true);
      });
  }
}

void
writefile(rfs::FileSystem& fs,
          std::string const& name,
          std::string const& content)
{
  auto handle = fs.path("/" + name)->create(O_RDWR, 0666 | S_IFREG);
  handle->write(elle::WeakBuffer((char*)content.data(), content.size()),
                content.size(), 0);
  handle->close();
  handle.reset();
}

void
appendfile(rfs::FileSystem& fs,
           std::string const& name,
           std::string const& content)
{
  auto handle = fs.path("/" + name)->open(O_RDWR, 0666 | S_IFREG);
  struct stat st;
  fs.path("/" + name)->stat(&st);
  handle->write(elle::WeakBuffer((char*)content.data(), content.size()),
                content.size(), st.st_size);
  handle->close();
  handle.reset();
}

std::string
readfile(rfs::FileSystem& fs,
         std::string const& name)
{
  auto handle = fs.path("/" + name)->open(O_RDONLY, S_IFREG);
  std::string res(32768, 0);
  int sz = handle->read(elle::WeakBuffer(elle::unconst(res.data()), 32768), 32768, 0);
  res.resize(sz);
  ELLE_TRACE("got %s bytes", sz);
  handle->close();
  handle.reset();
  return res;
}

ELLE_TEST_SCHEDULED(basic)
{
  auto d = elle::filesystem::TemporaryDirectory{};
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  ELLE_LOG("write files")
  {
    auto nodes = run_nodes(tmp, kp, 10 / valgrind(1, 2));
    auto fsp = make_observer(nodes.front().first, tmp, kp, 1, 3, false, false, false);
    auto& fs = fsp.first;
    writefile(*fs, "test1", "foo");
    writefile(*fs, "test2", std::string(32000, 'a'));
    BOOST_CHECK_EQUAL(readfile(*fs, "test1"), "foo");
    BOOST_CHECK_EQUAL(readfile(*fs, "test2"), std::string(32000, 'a'));
  }
  ELLE_LOG("read files")
  {
    auto nodes = run_nodes(tmp, kp, 10 / valgrind(1, 2));
    auto fs = make_observer(nodes.front().first, tmp, kp, 1, 3, false, false, false);
    BOOST_CHECK_EQUAL(readfile(*fs.first, "test1"), "foo");
    BOOST_CHECK_EQUAL(readfile(*fs.first, "test2"), std::string(32000, 'a'));
  }
}

static void make_files(rfs::FileSystem& fs, std::string const& name, int count)
{
  fs.path("/")->child(name)->mkdir(0);
  for (int i=0; i<count; ++i)
    fs.path("/")->child(name)->child(std::to_string(i))->create(O_CREAT | O_RDWR, 0);
}

static int dir_size(rfs::FileSystem& fs, std::string const& name)
{
  int count = 0;
  fs.path("/")->child(name)->list_directory(
    [&](std::string const& fname, struct stat*)
    {
      if (fname == "." || fname == "..")
        return;
      struct stat st;
      fs.path("/")->child(name)->child(fname)->stat(&st);
      ++count;
    });
  return count;
}

namespace
{
  /// \param count  number of nodes to create
  /// \param replication_factor   passed to each observer
  void
  check_list_directory(int count, int replication_factor)
  {
    elle::filesystem::TemporaryDirectory d;
    auto tmp = d.path();
    elle::os::setenv("INFINIT_HOME", tmp.string(), true);
    auto kp = elle::cryptography::rsa::keypair::generate(512);
    auto nodes = run_nodes(tmp, kp, count);
    auto fswrite = make_observer(nodes.front().first, tmp, kp, 1, replication_factor, true, false, false);
    auto fsc =     make_observer(nodes.front().first, tmp, kp, 1, replication_factor, true, false, false);
    auto fsca =    make_observer(nodes.front().first, tmp, kp, 1, replication_factor, true, true, false);
    auto fsa =     make_observer(nodes.front().first, tmp, kp, 1, replication_factor, false, true, false);

    ELLE_TRACE("write");
    make_files(*fswrite.first, "50", 50);
    ELLE_TRACE("list fsc");
    BOOST_TEST(dir_size(*fsc.first,  "50") == 50);
    ELLE_TRACE("list fsca");
    BOOST_TEST(dir_size(*fsca.first, "50") == 50);
    ELLE_TRACE("list fsa");
    BOOST_TEST(dir_size(*fsa.first,  "50") == 50);
    ELLE_TRACE("list fsc");
    BOOST_TEST(dir_size(*fsc.first,  "50") == 50);
    ELLE_TRACE("list fsca");
    BOOST_TEST(dir_size(*fsca.first, "50") == 50);
    ELLE_TRACE("list fsa");
    BOOST_TEST(dir_size(*fsa.first,  "50") == 50);
    ELLE_TRACE("done");

    ELLE_TRACE("kill fsa");
    fsa.first.reset();
    ELLE_TRACE("kill fsca");
    fsca.first.reset();
    ELLE_TRACE("kill fsc");
    fsc.first.reset();
    ELLE_TRACE("kill fswrite");
    fswrite.first.reset();
    ELLE_TRACE("kill nodes");
    nodes.clear();
    ELLE_TRACE("kill the rest");
  }
}

ELLE_TEST_SCHEDULED(list_directory)
{
  check_list_directory(1, 1);
}

ELLE_TEST_SCHEDULED(list_directory_3)
{
  check_list_directory(3, 3);
}

ELLE_TEST_SCHEDULED(list_directory_5_3)
{
  check_list_directory(5, 3);
}

// ELLE_TEST_SCHEDULED(conflictor)
// {
//   auto tmp = bfs::temp_directory_path() / bfs::unique_path();
//   elle::SafeFinally cleanup([&]
//     {
//       bfs::remove_all(tmp);
//     });
//   auto kp = elle::cryptography::rsa::keypair::generate(2048);
//   auto nodes = run_nodes(tmp, kp, 3, 1, 3, false);
//   std::vector<elle::reactor::Thread::unique_ptr> v;
//   std::vector<std::unique_ptr<rfs::FileSystem>> fss;
//   fss.reserve(3);
//   for (int i=0; i<3; ++i)
//   {
//     fss.push_back(make_observer(nodes.front(), tmp, kp, 1, 3, false, false, false));
//     if (i == 0)
//       writefile(*fss.front(), "foo", "bar");
//     elle::reactor::sleep(100_ms);
//   }
//   for (int i=0; i<3; ++i)
//   {
//     auto fs = fss[i].get();
//     v.emplace_back(new elle::reactor::Thread("process", [fs] {
//         for (int j=0; j<100; ++j)
//         {
//           auto h = fs->path("/")->child("foo")->open(O_RDWR | O_TRUNC | O_CREAT, S_IFREG);
//           h->write(elle::WeakBuffer((char*)"bar", 3), 3, 0);
//           h->close();
//           readfile(*fs, "foo");
//         }
//     }));
//   }
//   for (int i=0; i<3; ++i)
//     elle::reactor::wait(*v[i]);
//   ELLE_LOG("teardown");
// }

// void test_kill_nodes(int node_count, int k, int replication, int kill, bool overwrite_while_down, bool lenient)
// {
//   static const int file_count = 20;
//   ELLE_LOG("kill_nodes n=%s k=%s r=%s", node_count, kill, replication);
//   auto tmp = bfs::temp_directory_path() / bfs::unique_path();
//   elle::SafeFinally cleanup([&]
//     {
//       bfs::remove_all(tmp);
//     });
//   auto kp = elle::cryptography::rsa::keypair::generate(2048);
//   auto nodes = run_nodes(tmp, kp, node_count, k, replication, lenient);
//   auto fs = make_observer(nodes.front(), tmp, kp, k, replication, false, false, lenient);
//   ELLE_LOG("initial file write");
//   for (int i=0; i<file_count; ++i)
//     writefile(*fs, "foo" + std::to_string(i), "foo");
//   for (int i=0; i<file_count; ++i)
//     BOOST_CHECK_EQUAL(readfile(*fs, "foo" + std::to_string(i)), "foo");
//   ELLE_LOG("killing nodes");
//   for (int k=0; k<kill; ++k)
//   {
//     // We must kill the same nodes as the ones we wont reinstantiate below
//     int idx = nodes.size() - k - 1;
//     nodes[idx].reset();
//   }
//   ELLE_LOG("checking files");
//   for (int i=0; i<file_count; ++i)
//   {
//     std::cerr << "\r" << i << "                ";
//     BOOST_CHECK_EQUAL(readfile(*fs, "foo" + std::to_string(i)), "foo");
//   }
//   ELLE_LOG("write new files");
//   // WRITE
//   if (replication - kill > replication / 2)
//   for (int i=0; i<file_count; ++i)
//   {
//     std::cerr << "\r" << i << "                ";
//     writefile(*fs, "bar" + std::to_string(i), "bar");
//   }
//   if (overwrite_while_down)
//   {
//     ELLE_LOG("append existing files");
//     for (int i=0; i<file_count; ++i)
//     {
//       std::cerr << "\r" << i << "                ";
//       appendfile(*fs, "foo" + std::to_string(i), "foo");
//     }
//   }
//   // OVERWRITE
//   ELLE_LOG("teardown");
//   fs.reset();
//   nodes.clear();
//   ELLE_LOG("reinitialize");
//   nodes = run_nodes(tmp, kp, node_count - kill, 1, replication); // dont reload the last node
//   fs = make_observer(nodes.front(), tmp, kp, 1, replication, false, false, lenient);
//   if (overwrite_while_down)
//   {
//     ELLE_LOG("overwrite");
//     for (int i=0; i<file_count; ++i)
//     {
//       std::cerr << "\r" << i << "                ";
//       appendfile(*fs, "foo" + std::to_string(i), "foo");
//     }
//   }
//   ELLE_LOG("check");
//   auto value = overwrite_while_down ? "foofoofoo" : "foo";
//   for (int i=0; i<file_count; ++i)
//     BOOST_CHECK_EQUAL(readfile(*fs, "foo" + std::to_string(i)), value);
//   if (replication - kill > replication / 2)
//     for (int i=0; i<file_count; ++i)
//       BOOST_CHECK_EQUAL(readfile(*fs, "bar" + std::to_string(i)), "bar");
//   ELLE_LOG("final teardown");
// }

// ELLE_TEST_SCHEDULED(killed_nodes)
// {
//   test_kill_nodes(5, 1, 3, 1, true, false);
// }

// ELLE_TEST_SCHEDULED(killed_nodes_half_lenient)
// {
//   test_kill_nodes(5, 1, 2, 1, false, true);
// }

// ELLE_TEST_SCHEDULED(killed_nodes_k2)
// {
//   test_kill_nodes(15, 3, 3, 1, true, false);
// }

ELLE_TEST_SCHEDULED(conflicts)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  ELLE_LOG("write files")
  {
    auto nodes = run_nodes(tmp, kp, 1);
    auto fs1p = make_observer(nodes.front().first, tmp, kp, 1, 1, true, false, false);
    auto fs2p = make_observer(nodes.front().first, tmp, kp, 1, 1, true, false, false);
    auto& fs1 = fs1p.first;
    auto& fs2 = fs2p.first;
    auto cache1 =
      dynamic_cast<infinit::model::doughnut::consensus::Cache*>(
        dynamic_cast<infinit::model::doughnut::Doughnut*>(
          dynamic_cast<ifs::FileSystem*>(
           fs1->operations().get())->block_store().get())->consensus().get());
    auto cache2 =
      dynamic_cast<infinit::model::doughnut::consensus::Cache*>(
        dynamic_cast<infinit::model::doughnut::Doughnut*>(
          dynamic_cast<ifs::FileSystem*>(
           fs2->operations().get())->block_store().get())->consensus().get());
    struct stat st;
    fs1->path("/dir")->mkdir(0600);
    cache2->clear();
    fs2->path("/dir")->stat(&st);
    BOOST_CHECK_NO_THROW(writefile(*fs1, "dir/file", "foo"));
    cache2->clear();
    BOOST_CHECK_EQUAL(readfile(*fs2, "dir/file"), "foo");

    ELLE_LOG("conflict 1");
    BOOST_CHECK_NO_THROW(appendfile(*fs1, "dir/file", "bar"));
    BOOST_CHECK_NO_THROW(fs2->path("/dir/file")->chmod(0606));
    cache1->clear();
    cache2->clear();
    BOOST_CHECK_EQUAL(readfile(*fs2, "dir/file"), "foobar");
    BOOST_CHECK_NO_THROW(fs1->path("/dir/file")->stat(&st));
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0606);
    BOOST_CHECK_NO_THROW(fs2->path("/dir/file")->stat(&st));
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0606);

    ELLE_LOG("conflict 2");
    fs2->path("/dir/file")->chmod(0604);
    appendfile(*fs1, "dir/file", "bar");
    cache1->clear();
    cache2->clear();
    BOOST_CHECK_EQUAL(readfile(*fs2, "dir/file"), "foobarbar");
    fs1->path("/dir/file")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0604);

    ELLE_LOG("conflict 3");
    writefile(*fs1, "dir/file2", "foo");
    fs2->path("/dir")->chmod(0606);
    cache1->clear();
    cache2->clear();
    BOOST_CHECK_EQUAL(readfile(*fs2, "dir/file2"), "foo");
    fs1->path("/dir")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_mode & 0666, 0606);

    ELLE_LOG("conflict 4");
    fs2->path("/dir")->chmod(0604);
    writefile(*fs1, "dir/file3", "foo");
    cache1->clear();
    cache2->clear();
    BOOST_CHECK_EQUAL(readfile(*fs2, "dir/file3"), "foo");
    fs1->path("/dir")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_mode & 0666, 0604);

    ELLE_LOG("conflict 5");
    writefile(*fs1, "dir/c51", "foo");
    writefile(*fs2, "dir/c52", "foo");
    cache1->clear();
    cache2->clear();
    BOOST_CHECK_EQUAL(readfile(*fs2, "dir/c51"), "foo");
    BOOST_CHECK_EQUAL(readfile(*fs1, "dir/c52"), "foo");
  }
}

ELLE_TEST_SCHEDULED(times)
{
  elle::filesystem::TemporaryDirectory d;
  auto const tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto const kp = elle::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 1);
  auto fsp
    = make_observer(nodes.front().first, tmp, kp, 1, 1, false, false, false);
  auto& fs = fsp.first;
  struct stat st;
  // We only have second resolution, so test in batches to avoid too
  // much sleeping.
  auto const delta = valgrind(1, 5);
  fs->path("/dir")->mkdir(0600);
  fs->path("/dir2")->mkdir(0600);
  writefile(*fs, "dir/file", "foo");
  writefile(*fs, "dir2/file", "foo");
  fs->path("/dir")->stat(&st);
  auto now = time(nullptr);
  BOOST_TEST(now - st.st_mtime <= delta);
  BOOST_TEST(now - st.st_ctime <= delta);
  fs->path("/dir/file")->stat(&st);
  BOOST_TEST(now - st.st_mtime <= delta);
  BOOST_TEST(now - st.st_ctime <= delta);

  elle::reactor::sleep(2100_ms);
  now = time(nullptr);
  appendfile(*fs, "dir/file", "foo"); //mtime changed, ctime unchanged, dir unchanged
  fs->path("/dir/file")->stat(&st);
  BOOST_TEST(now - st.st_mtime <= delta);
  BOOST_TEST(now - st.st_ctime >= 2);
  fs->path("/dir")->stat(&st);
  BOOST_TEST(now - st.st_mtime >= 2);
  BOOST_TEST(now - st.st_ctime >= 2);

  elle::reactor::sleep(2100_ms);
  now = time(nullptr);
  writefile(*fs, "dir/file2", "foo");
  fs->path("/dir2/dir")->mkdir(0600);
  fs->path("/dir/file")->stat(&st);
  BOOST_TEST(now - st.st_mtime >= 2);
  BOOST_TEST(now - st.st_ctime >= 2);
  fs->path("/dir")->stat(&st); // new file created: mtime change
  BOOST_TEST(now - st.st_mtime <= delta);
  BOOST_TEST(now - st.st_ctime >= 2);
  fs->path("/dir2")->stat(&st); // new dir created: mtime change
  BOOST_TEST(now - st.st_mtime <= delta);
  BOOST_TEST(now - st.st_ctime >= 2);
}

#define CHECKED(exp) \
try { exp} catch (std::exception const& e) { ELLE_WARN("%s", e.what());  throw;}
ELLE_TEST_SCHEDULED(clients_parallel)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 4, /*k*/1, /*repfactor*/1);
  auto fss = node_to_fs(nodes);
  fss.front()->path("/");
  elle::reactor::for_each_parallel(fss, [&](std::unique_ptr<rfs::FileSystem>& fs)
    {
      auto p = std::to_string((uint64_t)fs.get());
      CHECKED(fs->path("/" + p)->mkdir(0666);)
      CHECKED(fs->path("/" + p + "/0")->mkdir(0666);)
    });
  for(auto const& n: fss)
  {
    std::vector<std::string> items;
    n->path("/")->list_directory([&] (std::string const& n, struct stat* stbuf)
      {
        items.push_back(n);
      });
    ELLE_LOG("%x: %s", n.get(), items);
    BOOST_TEST(items.size() == fss.size()+2);
  }
  for (auto const& n: fss)
    for (auto const& t: fss)
    {
      auto p = std::to_string((uint64_t)t.get());
      struct stat st;
      n->path("/" + p +"/0")->stat(&st);
      BOOST_TEST(S_ISDIR(st.st_mode));
    }
}

ELLE_TEST_SCHEDULED(many_conflicts)
{
  static const int node_count = 4;
  static const int iter_count = 50;
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, node_count, /*k*/1, /*repfactor*/3);
  auto fss = node_to_fs(nodes);
  fss.front()->path("/");
  elle::reactor::for_each_parallel(fss, [&](std::unique_ptr<rfs::FileSystem>& fs)
    {
      for (int i=0; i<iter_count; ++i)
      {
        fs->path("/" + std::to_string(i) + "_" + std::to_string((uint64_t)&fs))->mkdir(0666);
      }
  });
  for (int j=0; j<node_count; ++j)
  {
    int count = 0;
    fss[j]->path("/")->list_directory([&] (std::string const& n, struct stat* stbuf)
      {
        ++count;
      });
    BOOST_TEST(count == iter_count * node_count + 2);
  }
}

ELLE_TEST_SCHEDULED(remove_conflicts)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 2, /*k*/1, /*repfactor*/1);
  auto fss = node_to_fs(nodes);
  fss.front()->path("/");
  // Don't try to simplify, of all those runs only two
  // trigger an edit conflict
  for (int i=0; i<100; ++i)
  {
    fss[0]->path("/foo")->mkdir(0666);
    std::vector<int> is{0, 1};
    elle::reactor::for_each_parallel(is, [i,&fss](int s) {
      int which = (i/2)%2;
      if (s == (i%2))
        try {
          for (int y=0; y<i%10; ++y)
            elle::reactor::yield();
          fss[which]->path("/foo")->setxattr("bar", "baz", 0);
        }
        catch (elle::reactor::filesystem::Error const&)
        {}
      else
      {
        for (int y=0; y<i/10; ++y)
          elle::reactor::yield();
        fss[1-which]->path("/foo")->rmdir();
      }
    });
  }
}

ELLE_TEST_SCHEDULED(beyond_observer_1)
{
  Beyond beyond;
  elle::filesystem::TemporaryDirectory d;
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(d.path(), kp, 2, 1, 2, false, beyond.port(), 0);
  auto fsp = make_observer(nodes.front().first, d.path(), kp, 1, 2, false, false, false, beyond.port());
  auto& fs = fsp.first;
  beyond.push(*nodes[0].first);
  beyond.push(*nodes[1].first);
  writefile(*fs, "file", "foo");
  BOOST_CHECK_EQUAL(readfile(*fs, "file"), "foo");
  // update
  beyond.pull(*nodes[0].first);
  beyond.pull(*nodes[1].first);
  nodes.clear();
  BOOST_CHECK_THROW(readfile(*fs, "file"), std::exception);
  nodes = run_nodes(d.path(), kp, 2, 1, 2, false, beyond.port(), 0);
  beyond.push(*nodes[0].first);
  beyond.push(*nodes[1].first);
  BOOST_CHECK_NO_THROW(insist([&] { readfile(*fs, "file");}));

  // remove then add
  beyond.pull(*nodes[0].first);
  beyond.pull(*nodes[1].first);
  nodes.clear();
  elle::reactor::sleep(2_sec);
  BOOST_CHECK_THROW(readfile(*fs, "file"), std::exception);
  nodes = run_nodes(d.path(), kp, 2, 1, 2, false, beyond.port(), 0);
  beyond.push(*nodes[0].first);
  beyond.push(*nodes[1].first);
  BOOST_CHECK_NO_THROW(insist([&] { readfile(*fs, "file");}));
}



ELLE_TEST_SCHEDULED(beyond_observer_2)
{
  Beyond beyond;
  elle::filesystem::TemporaryDirectory d;
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(d.path(), kp, 2, 1, 2, false, beyond.port(), 0);
  auto fsp = make_observer(nodes.front().first, d.path(), kp, 1, 2, false, false, false, beyond.port());
  auto& fs = fsp.first;
  beyond.push(*nodes[0].first);
  beyond.push(*nodes[1].first);
  writefile(*fs, "file", "foo");
  BOOST_CHECK_EQUAL(readfile(*fs, "file"), "foo");
  beyond.pull(*nodes[0].first);
  nodes[0].first.reset();
  nodes[0].second.reset();
  BOOST_CHECK_THROW(writefile(*fs, "file", "bar"), std::exception);
  nodes[0] = std::move(run_nodes(d.path(), kp, 1, 1, 2, false, beyond.port(), 0)[0]);
  beyond.push(*nodes[0].first);
  insist([&] { writefile(*fs, "file", "bar");});
  BOOST_TEST(readfile(*fs, "file") == "bar");
}

ELLE_TEST_SCHEDULED(beyond_storage)
{
  // test reconnection between storages
  Beyond beyond;
  elle::filesystem::TemporaryDirectory d;
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(d.path(), kp, 2, 1, 1, false, beyond.port(), 0);
  auto fsp = make_observer(nodes.front().first, d.path(), kp, 1, 1, false, false, false, beyond.port());
  auto& fs = fsp.first;
  auto files = std::vector<std::string>{};
  for (int i=0; i<20; ++i)
    files.emplace_back("file" + std::to_string(i));
  for (auto const& f : files)
    writefile(*fs, f, "foo");
  for (auto const& f : files)
  {
    ELLE_LOG("file %s", f);
    BOOST_TEST(readfile(*fs, f) == "foo");
  }
  nodes[0].first.reset();
  nodes[0].second.reset();
  nodes[0] = std::move(run_nodes(d.path(), kp, 1, 1, 1, false, beyond.port(), 0)[0]);
  beyond.push(*nodes[0].first);
  // If the nodes see each other they will route requests to one
  // another, otherwise requests will fail (we set a low
  // query_get_retries)
  for (auto const& f : files)
    BOOST_TEST(insist([&]{
          ELLE_LOG("file %s", f);
          return readfile(*fs, f);
        }) == "foo");

  // same operation, but push the other one on beyond
  // why does it work? node0 upon restart will find node1 from beyond
  // and bootstrap to it. Then node1 will be able to answer the observer
  // when it will do a node lookup for node0
  beyond.pull(*nodes[0].first);
  nodes[0].first.reset();
  nodes[0].second.reset();
  nodes[0] = std::move(run_nodes(d.path(), kp, 1, 1, 1, false, beyond.port(), 0)[0]);
  beyond.push(*nodes[1].first);
  for (auto const& f : files)
    BOOST_CHECK_NO_THROW(insist([&] {
          ELLE_LOG("file %s", f);
          readfile(*fs, f);
        }));
}


ELLE_TEST_SUITE()
{
  srand(time(nullptr));
  elle::os::setenv("INFINIT_CONNECT_TIMEOUT", "2", 1);
  elle::os::setenv("INFINIT_SOFTFAIL_TIMEOUT", "5", 1);
  // disable RDV so that nodes won't find each other that way
  elle::os::setenv("INFINIT_RDV", "", 1);
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(basic), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(conflicts), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(times), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(list_directory), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(list_directory_3), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(list_directory_5_3), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(clients_parallel), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(many_conflicts), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(remove_conflicts), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(beyond_observer_1), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(beyond_observer_2), 0, valgrind(120));
  suite.add(BOOST_TEST_CASE(beyond_storage), 0, valgrind(120));
  // suite.add(BOOST_TEST_CASE(killed_nodes), 0, 600);
  //suite.add(BOOST_TEST_CASE(killed_nodes_half_lenient), 0, 600);
  // suite.add(BOOST_TEST_CASE(killed_nodes_k2), 0, 600);
  // suite.add(BOOST_TEST_CASE(conflictor), 0, 300);
}
