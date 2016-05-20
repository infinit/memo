#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/test.hh>

#include <reactor/for-each.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Storage.hh>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>

#ifdef INFINIT_WINDOWS
# undef stat
#endif

ELLE_LOG_COMPONENT("test");

namespace ifs = infinit::filesystem;
namespace rfs = reactor::filesystem;
namespace bfs = boost::filesystem;
namespace imd = infinit::model::doughnut;
namespace iok = infinit::overlay::kelips;

infinit::overlay::NodeEndpoints endpoints;

static std::vector<std::shared_ptr<imd::Doughnut>>
run_nodes(bfs::path where,  infinit::cryptography::rsa::KeyPair& kp,
          int count = 10, int groups = 1, int replication_factor = 3,
          bool paxos_lenient = false)
{
  ELLE_LOG_SCOPE("building and running %s nodes", count);
  endpoints.clear();
  std::vector<std::shared_ptr<imd::Doughnut>> res;
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
          return elle::make_unique<imd::consensus::Paxos>(dht, replication_factor, paxos_lenient);
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
  }
  return res;
}

static std::unique_ptr<rfs::FileSystem>
make_observer(std::shared_ptr<imd::Doughnut>& root_node,
              bfs::path where,
              infinit::cryptography::rsa::KeyPair& kp,
              int groups,
              int replication_factor,
              bool cache, bool async,
              bool paxos_lenient)
{
  ELLE_LOG_SCOPE("building observer");
  iok::Configuration config;
  config.k = groups;
  config.encrypt = true;
  config.accept_plain = false;
  infinit::model::doughnut::Passport passport(kp.K(), "testnet", kp);
  infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
  [&] (infinit::model::doughnut::Doughnut& dht)
  -> std::unique_ptr<imd::consensus::Consensus>
  {
    std::unique_ptr<imd::consensus::Consensus> backend =
      elle::make_unique<imd::consensus::Paxos>(dht, replication_factor, paxos_lenient);
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
    return backend;
  };
  infinit::model::doughnut::Doughnut::OverlayBuilder overlay =
  [&] (infinit::model::doughnut::Doughnut& dht,
    infinit::model::Address id,
    std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return config.make(id, endpoints, local, &dht);
  };
  auto dn = std::make_shared<infinit::model::doughnut::Doughnut>(
    infinit::model::Address::random(0), // FIXME
    std::make_shared<infinit::cryptography::rsa::KeyPair>(kp),
    kp.public_key(),
    passport,
    consensus,
    overlay,
    boost::optional<int>(),
    std::unique_ptr<infinit::storage::Storage>());
  auto ops = elle::make_unique<infinit::filesystem::FileSystem>("volume", dn);
  auto fs = elle::make_unique<reactor::filesystem::FileSystem>(std::move(ops), true);
  ELLE_LOG("Returning observer");
  return fs;
}

static std::vector<std::unique_ptr<rfs::FileSystem>>
node_to_fs(std::vector<std::shared_ptr<imd::Doughnut>> const& nodes)
{
  std::vector<std::unique_ptr<rfs::FileSystem>> res;
  for (auto n: nodes)
  {
    auto ops = elle::make_unique<infinit::filesystem::FileSystem>("volume",n);
    auto fs = elle::make_unique<reactor::filesystem::FileSystem>(std::move(ops), true);
    res.push_back(std::move(fs));
  }
  return res;
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
  return res;
}

ELLE_TEST_SCHEDULED(basic)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  ELLE_LOG("write files")
  {
    auto nodes = run_nodes(tmp, kp, 10 / valgrind(1, 2));
    auto fs = make_observer(nodes.front(), tmp, kp, 1, 3, false, false, false);
    writefile(*fs, "test1", "foo");
    writefile(*fs, "test2", std::string(32000, 'a'));
    BOOST_CHECK_EQUAL(readfile(*fs, "test1"), "foo");
    BOOST_CHECK_EQUAL(readfile(*fs, "test2"), std::string(32000, 'a'));
  }
  ELLE_LOG("read files")
  {
    auto nodes = run_nodes(tmp, kp, 10 / valgrind(1, 2));
    auto fs = make_observer(nodes.front(), tmp, kp, 1, 3, false, false, false);
    BOOST_CHECK_EQUAL(readfile(*fs, "test1"), "foo");
    BOOST_CHECK_EQUAL(readfile(*fs, "test2"), std::string(32000, 'a'));
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
      struct stat st;
      fs.path("/")->child(name)->child(fname)->stat(&st);
      ++count;
    });
  return count;
}

ELLE_TEST_SCHEDULED(list_directory)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 1);
  auto fswrite = make_observer(nodes.front(), tmp, kp, 1, 1, true, false, false);
  auto fsc = make_observer(nodes.front(), tmp, kp, 1, 1, true, false, false);
  auto fsca = make_observer(nodes.front(), tmp, kp, 1, 1, true, true, false);
  auto fsa = make_observer(nodes.front(), tmp, kp, 1, 1, false, true, false);
  ELLE_TRACE("write");
  make_files(*fswrite, "50", 50);
  ELLE_TRACE("list fsc");
  ELLE_ASSERT_EQ(dir_size(*fsc,  "50"), 50);
  ELLE_TRACE("list fsca");
  ELLE_ASSERT_EQ(dir_size(*fsca, "50"), 50);
  ELLE_TRACE("list fsa");
  ELLE_ASSERT_EQ(dir_size(*fsa,  "50"), 50);
  ELLE_TRACE("list fsc");
  ELLE_ASSERT_EQ(dir_size(*fsc,  "50"), 50);
  ELLE_TRACE("list fsca");
  ELLE_ASSERT_EQ(dir_size(*fsca, "50"), 50);
  ELLE_TRACE("list fsa");
  ELLE_ASSERT_EQ(dir_size(*fsa,  "50"), 50);
  ELLE_TRACE("done");
}

ELLE_TEST_SCHEDULED(list_directory_3)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 3);
  auto fswrite = make_observer(nodes.front(), tmp, kp, 1, 3, true, false, false);
  auto fsc = make_observer(nodes.front(), tmp, kp, 1, 3, true, false, false);
  auto fsca = make_observer(nodes.front(), tmp, kp, 1, 3, true, true, false);
  auto fsa = make_observer(nodes.front(), tmp, kp, 1, 3, false, true, false);
  ELLE_TRACE("write");
  make_files(*fswrite, "50", 50);
  ELLE_TRACE("list fsc");
  ELLE_ASSERT_EQ(dir_size(*fsc,  "50"), 50);
  ELLE_TRACE("list fsca");
  ELLE_ASSERT_EQ(dir_size(*fsca, "50"), 50);
  ELLE_TRACE("list fsa");
  ELLE_ASSERT_EQ(dir_size(*fsa,  "50"), 50);
  ELLE_TRACE("list fsc");
  ELLE_ASSERT_EQ(dir_size(*fsc,  "50"), 50);
  ELLE_TRACE("list fsca");
  ELLE_ASSERT_EQ(dir_size(*fsca, "50"), 50);
  ELLE_TRACE("list fsa");
  ELLE_ASSERT_EQ(dir_size(*fsa,  "50"), 50);
  ELLE_TRACE("done");
}

ELLE_TEST_SCHEDULED(list_directory_5_3)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 5, 1, 3);
  auto fswrite = make_observer(nodes.front(), tmp, kp, 1, 3, true, false, false);
  auto fsc = make_observer(nodes.front(), tmp, kp, 1, 3, true, false, false);
  auto fsca = make_observer(nodes.front(), tmp, kp, 1, 3, true, true, false);
  auto fsa = make_observer(nodes.front(), tmp, kp, 1, 3, false, true, false);
  ELLE_TRACE("write");
  make_files(*fswrite, "50", 50);
  ELLE_TRACE("list fsc");
  ELLE_ASSERT_EQ(dir_size(*fsc,  "50"), 50);
  ELLE_TRACE("list fsca");
  ELLE_ASSERT_EQ(dir_size(*fsca, "50"), 50);
  ELLE_TRACE("list fsa");
  ELLE_ASSERT_EQ(dir_size(*fsa,  "50"), 50);
  ELLE_TRACE("list fsc");
  ELLE_ASSERT_EQ(dir_size(*fsc,  "50"), 50);
  ELLE_TRACE("list fsca");
  ELLE_ASSERT_EQ(dir_size(*fsca, "50"), 50);
  ELLE_TRACE("list fsa");
  ELLE_ASSERT_EQ(dir_size(*fsa,  "50"), 50);
  ELLE_TRACE("done");
}
// ELLE_TEST_SCHEDULED(conflictor)
// {
//   auto tmp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
//   elle::SafeFinally cleanup([&]
//     {
//       boost::filesystem::remove_all(tmp);
//     });
//   auto kp = infinit::cryptography::rsa::keypair::generate(2048);
//   auto nodes = run_nodes(tmp, kp, 3, 1, 3, false);
//   std::vector<reactor::Thread::unique_ptr> v;
//   std::vector<std::unique_ptr<rfs::FileSystem>> fss;
//   fss.reserve(3);
//   for (int i=0; i<3; ++i)
//   {
//     fss.push_back(make_observer(nodes.front(), tmp, kp, 1, 3, false, false, false));
//     if (i == 0)
//       writefile(*fss.front(), "foo", "bar");
//     reactor::sleep(100_ms);
//   }
//   for (int i=0; i<3; ++i)
//   {
//     auto fs = fss[i].get();
//     v.emplace_back(new reactor::Thread("process", [fs] {
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
//     reactor::wait(*v[i]);
//   ELLE_LOG("teardown");
// }

// void test_kill_nodes(int node_count, int k, int replication, int kill, bool overwrite_while_down, bool lenient)
// {
//   static const int file_count = 20;
//   ELLE_LOG("kill_nodes n=%s k=%s r=%s", node_count, kill, replication);
//   auto tmp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
//   elle::SafeFinally cleanup([&]
//     {
//       boost::filesystem::remove_all(tmp);
//     });
//   auto kp = infinit::cryptography::rsa::keypair::generate(2048);
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
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  ELLE_LOG("write files")
  {
    auto nodes = run_nodes(tmp, kp, 1);
    auto fs1 = make_observer(nodes.front(), tmp, kp, 1, 1, true, false, false);
    auto fs2 = make_observer(nodes.front(), tmp, kp, 1, 1, true, false, false);
    auto cache1 =
      dynamic_cast<infinit::model::doughnut::consensus::Cache*>(
        dynamic_cast<infinit::model::doughnut::Doughnut*>(
          dynamic_cast<infinit::filesystem::FileSystem*>(
           fs1->operations().get())->block_store().get())->consensus().get());
    auto cache2 =
      dynamic_cast<infinit::model::doughnut::consensus::Cache*>(
        dynamic_cast<infinit::model::doughnut::Doughnut*>(
          dynamic_cast<infinit::filesystem::FileSystem*>(
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
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 1);
  auto fs = make_observer(nodes.front(), tmp, kp, 1, 1, false, false, false);
  struct stat st;
  // we only have second resolution, so test in batches to avoid too much sleeping
  fs->path("/dir")->mkdir(0600);
  fs->path("/dir2")->mkdir(0600);
  writefile(*fs, "dir/file", "foo");
  writefile(*fs, "dir2/file", "foo");
  fs->path("/dir")->stat(&st);
  auto now = time(nullptr);
  BOOST_CHECK(now - st.st_mtime <= 1);
  BOOST_CHECK(now - st.st_ctime <= 1);
  fs->path("/dir/file")->stat(&st);
  BOOST_CHECK(now - st.st_mtime <= 1);
  BOOST_CHECK(now - st.st_ctime <= 1);

  reactor::sleep(2100_ms);
  now = time(nullptr);
  appendfile(*fs, "dir/file", "foo"); //mtime changed, ctime unchanged, dir unchanged
  fs->path("/dir/file")->stat(&st);
  BOOST_CHECK(now - st.st_mtime <= 1);
  BOOST_CHECK(now - st.st_ctime >= 2);
  fs->path("/dir")->stat(&st);
  BOOST_CHECK(now - st.st_mtime >= 2);
  BOOST_CHECK(now - st.st_ctime >= 2);

  reactor::sleep(2100_ms);
  now = time(nullptr);
  writefile(*fs, "dir/file2", "foo");
  fs->path("/dir2/dir")->mkdir(0600);
  fs->path("/dir/file")->stat(&st);
  BOOST_CHECK(now - st.st_mtime >= 2);
  BOOST_CHECK(now - st.st_ctime >= 2);
  fs->path("/dir")->stat(&st); // new file created: mtime change
  BOOST_CHECK(now - st.st_mtime <= 1);
  BOOST_CHECK(now - st.st_ctime >= 2);
  fs->path("/dir2")->stat(&st); // new dir created: mtime change
  BOOST_CHECK(now - st.st_mtime <= 1);
  BOOST_CHECK(now - st.st_ctime >= 2);
}

#define CHECKED(exp) \
try { exp} catch (std::exception const& e) { ELLE_WARN("%s", e.what());  throw;}
ELLE_TEST_SCHEDULED(clients_parallel)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 4, /*k*/1, /*repfactor*/1);
  auto fss = node_to_fs(nodes);
  fss.front()->path("/");
  reactor::for_each_parallel(fss, [&](std::unique_ptr<rfs::FileSystem>& fs)
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
    BOOST_CHECK(items.size() == fss.size());
  }
  for(auto const& n: fss)
  {
    for (auto const& t: fss)
    {
      auto p = std::to_string((uint64_t)t.get());
      struct stat st;
      n->path("/" + p +"/0")->stat(&st);
      BOOST_CHECK(S_ISDIR(st.st_mode));
    }
  }
}

ELLE_TEST_SCHEDULED(many_conflicts)
{
  static const int node_count = 4;
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, node_count, /*k*/1, /*repfactor*/3);
  auto fss = node_to_fs(nodes);
  fss.front()->path("/");
  reactor::for_each_parallel(fss, [&](std::unique_ptr<rfs::FileSystem>& fs)
    {
      for (int i=0; i<100; ++i)
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
    BOOST_CHECK(count == 100 * node_count);
  }
}

ELLE_TEST_SCHEDULED(remove_conflicts)
{
  elle::filesystem::TemporaryDirectory d;
  auto tmp = d.path();
  elle::os::setenv("INFINIT_HOME", tmp.string(), true);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
  auto nodes = run_nodes(tmp, kp, 2, /*k*/1, /*repfactor*/1);
  auto fss = node_to_fs(nodes);
  fss.front()->path("/");
  // Don't try to simplify, of all those runs only two
  // trigger an edit conflict
  for (int i=0; i<100; ++i)
  {
    fss[0]->path("/foo")->mkdir(0666);
    std::vector<int> is{0, 1};
    reactor::for_each_parallel(is, [i,&fss](int s) {
      int which = (i/2)%2;
      if (s == (i%2))
        try {
          for (int y=0; y<i%10; ++y)
            reactor::yield();
          fss[which]->path("/foo")->setxattr("bar", "baz", 0);
        }
        catch (reactor::filesystem::Error const&)
        {}
      else
      {
        for (int y=0; y<i/10; ++y)
          reactor::yield();
        fss[1-which]->path("/foo")->rmdir();
      }
    });
  }
}

ELLE_TEST_SUITE()
{
  srand(time(nullptr));
  elle::os::setenv("INFINIT_CONNECT_TIMEOUT", "1", 1);
  elle::os::setenv("INFINIT_SOFTFAIL_TIMEOUT", "2", 1);
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(basic), 0, valgrind(60));
  suite.add(BOOST_TEST_CASE(conflicts), 0, valgrind(32));
  suite.add(BOOST_TEST_CASE(times), 0, valgrind(32));
  suite.add(BOOST_TEST_CASE(list_directory), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(list_directory_3), 0, valgrind(60));
  suite.add(BOOST_TEST_CASE(list_directory_5_3), 0, valgrind(60));
  suite.add(BOOST_TEST_CASE(clients_parallel), 0, valgrind(60));
  suite.add(BOOST_TEST_CASE(many_conflicts), 0, valgrind(60));
  suite.add(BOOST_TEST_CASE(remove_conflicts), 0, valgrind(60));
  // suite.add(BOOST_TEST_CASE(killed_nodes), 0, 600);
  //suite.add(BOOST_TEST_CASE(killed_nodes_half_lenient), 0, 600);
  // suite.add(BOOST_TEST_CASE(killed_nodes_k2), 0, 600);
  // suite.add(BOOST_TEST_CASE(conflictor), 0, 300);
}
