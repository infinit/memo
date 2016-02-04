#include <elle/test.hh>
#include <elle/json/json.hh>
#include <elle/serialization/json.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/overlay/Kalimero.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Storage.hh>


ELLE_LOG_COMPONENT("test");

namespace ifs = infinit::filesystem;
namespace rfs = reactor::filesystem;
namespace bfs = boost::filesystem;

std::unique_ptr<reactor::filesystem::FileSystem> make(
  bfs::path where,
  bool enable_async,
  int cache_size,
  infinit::cryptography::rsa::KeyPair& kp)
{
  std::unique_ptr<infinit::storage::Storage> s;
  boost::filesystem::create_directories(where / "store");
  boost::filesystem::create_directories(where / "async");
  s.reset(new infinit::storage::Filesystem(where / "store"));
  infinit::model::doughnut::Passport passport(kp.K(), "testnet", kp.k());
  infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
    [&] (infinit::model::doughnut::Doughnut& dht)
        -> std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
        {
           auto backend = elle::make_unique<
             infinit::model::doughnut::consensus::Consensus>(dht);
           if (!enable_async)
             return backend;
           auto async = elle::make_unique<
             infinit::model::doughnut::consensus::Async>(std::move(backend),
               where / "async", cache_size);
           return std::move(async);
        };
  infinit::model::doughnut::Doughnut::OverlayBuilder overlay =
        [=] (infinit::model::doughnut::Doughnut& dht,
             infinit::model::Address id,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          return elle::make_unique<infinit::overlay::Kalimero>(&dht, id, local);
        };
  auto dn = std::make_shared<infinit::model::doughnut::Doughnut>(
    infinit::model::Address::random(),
    std::make_shared<infinit::cryptography::rsa::KeyPair>(kp),
    kp.public_key(),
    passport,
    consensus,
    overlay,
    boost::optional<int>(),
    std::move(s));
  auto ops = elle::make_unique<infinit::filesystem::FileSystem>("volume", dn);
  auto fs = elle::make_unique<reactor::filesystem::FileSystem>(std::move(ops), true);
  return fs;
}

void cleanup(bfs::path where)
{
  boost::filesystem::remove_all(where);
}

void writefile(std::unique_ptr<reactor::filesystem::FileSystem>& fs,
               std::string const& name,
               std::string const& content)
{
  auto handle = fs->path("/")->child(name)->create(O_RDWR, 0666 | S_IFREG);
  handle->write(elle::WeakBuffer((char*)content.data(), content.size()),
                content.size(), 0);
  handle->close();
  handle.reset();
}
int root_count(std::unique_ptr<reactor::filesystem::FileSystem>& fs)
{
  int count = 0;
  fs->path("/")->list_directory([&](std::string const&, struct stat*)
    { ++count;});
  return count;
}
ELLE_TEST_SCHEDULED(async_cache)
{
  auto path = bfs::temp_directory_path() / bfs::unique_path();
  auto kp = infinit::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::SafeFinally cleanup_path([&] {
      boost::filesystem::remove_all(path);
  });
  // test that re-instantiating everything works
  auto fs = make(path, true, 10, kp);
  auto root = fs->path("/");
  auto handle = root->child("foo")->create(O_RDWR, 0666 | S_IFREG);
  handle->write(elle::WeakBuffer((char*)"bar", 3), 3, 0);
  handle->close();
  handle.reset();
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  root.reset();
  fs.reset();
  fs = make(path, true, 10, kp);
  int count = 0;
  fs->path("/")->list_directory([&](std::string const&, struct stat*)
    { ++count;});
  BOOST_CHECK_EQUAL(count, 1);
  handle = fs->path("/")->child("foo")->open(O_RDONLY, 0666);
  char buf[10] = {0};
  handle->read(elle::WeakBuffer(buf, 3), 3, 0);
  handle->close();
  handle.reset();
  BOOST_CHECK_EQUAL(std::string(buf), "bar");

  ELLE_LOG("basic journal");
  setenv("INFINIT_ASYNC_NOPOP", "1", 1);
  fs.reset();
  fs = make(path, true, 10, kp);
  handle = fs->path("/")->child("foo2")->create(O_RDWR, 0666 | S_IFREG);
  handle->write(elle::WeakBuffer((char*)"bar", 3), 3, 0);
  handle->close();
  handle = fs->path("/")->child("foo")->open(O_RDWR, 0666);
  handle->write(elle::WeakBuffer((char*)"bar", 3), 3, 3);
  handle->close();
  handle.reset();
  fs->path("/")->child("dir")->mkdir(777 | S_IFDIR);
  unsetenv("INFINIT_ASYNC_NOPOP");
  fs.reset();
  fs = make(path, true, 10, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  handle = fs->path("/")->child("foo")->open(O_RDWR, 0666);
  handle->read(elle::WeakBuffer(buf, 6), 6, 0);
  handle->close();
  handle.reset();
  buf[7] = 0;
  BOOST_CHECK_EQUAL(std::string(buf), "barbar");
  count = 0;
  fs->path("/")->list_directory([&](std::string const&, struct stat*)
    { ++count;});
  BOOST_CHECK_EQUAL(count, 3);
  reactor::sleep(100_ms); // prefetcher threads
  fs.reset();

  ELLE_LOG("conflict dir");
  setenv("INFINIT_ASYNC_NOPOP", "1", 1);
  fs = make(path, true, 10, kp);
  // queue a file creation
  writefile(fs, "file", "foo");
  fs.reset();
  // create another file in the same dir
  fs = make(path, false, 0, kp);
  writefile(fs, "file2", "bar");
  fs.reset();
  unsetenv("INFINIT_ASYNC_NOPOP");
  // restart with async which will dequeue
  fs = make(path, true, 10, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  BOOST_CHECK_EQUAL(root_count(fs), 5);
  reactor::sleep(100_ms);
  fs.reset();

  ELLE_LOG("conflict dir 2");
  setenv("INFINIT_ASYNC_NOPOP", "1", 1);
  fs = make(path, true, 10, kp);
  // queue a file creation
  writefile(fs, "samefile", "foo");
  fs.reset();
  // create same file in the same dir
  fs = make(path, false, 0, kp);
  writefile(fs, "samefile", "bar");
  fs.reset();
  unsetenv("INFINIT_ASYNC_NOPOP");
  // restart with async which will dequeue
  fs = make(path, true, 10, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  BOOST_CHECK_EQUAL(root_count(fs), 6);
  reactor::sleep(100_ms);
  fs.reset();

  ELLE_LOG("conflict file");
  setenv("INFINIT_ASYNC_NOPOP", "1", 1);
  fs = make(path, true, 10, kp);
  // queue a file creation
  writefile(fs, "samefile", "foo");
  fs.reset();
  // create same file in the same dir
  fs = make(path, false, 0, kp);
  writefile(fs, "samefile", "bar");
  fs.reset();
  unsetenv("INFINIT_ASYNC_NOPOP");
  // restart with async which will dequeue
  fs = make(path, true, 10, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  BOOST_CHECK_EQUAL(root_count(fs), 6);
  struct stat st;
  fs->path("/")->child("samefile")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_size, 3);
  reactor::sleep(100_ms);
  fs.reset();

  ELLE_LOG("ACL conflict");
  auto kp2 = infinit::cryptography::rsa::keypair::generate(1024);
  auto pub2 = elle::serialization::json::serialize(kp2.K());
    setenv("INFINIT_ASYNC_NOPOP", "1", 1);
  fs = make(path, true, 10, kp);
  // queue a attr change
  fs->path("/")->child("samefile")->setxattr("infinit.auth.setrw",
    std::string((const char*)pub2.contents(), pub2.size()), 0);
  fs.reset();
  // write same file in the same dir
  fs = make(path, false, 0, kp);
  writefile(fs, "samefile", "bar");
  fs.reset();
  unsetenv("INFINIT_ASYNC_NOPOP");
  // restart with async which will dequeue
  fs = make(path, true, 10, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  BOOST_CHECK_EQUAL(root_count(fs), 6);
  auto auth = fs->path("/")->child("samefile")->getxattr("user.infinit.auth");
  std::stringstream sauth(auth);
  auto jauth = elle::json::read(sauth);
  BOOST_CHECK_EQUAL(boost::any_cast<elle::json::Array>(jauth).size(), 2);
  reactor::sleep(100_ms);
  fs.reset();
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(async_cache), 0, valgrind(10));
}
