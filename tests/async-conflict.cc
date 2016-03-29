#include <elle/test.hh>
#include <elle/json/json.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/json.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/overlay/Kalimero.hh>
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

std::unique_ptr<reactor::filesystem::FileSystem>
make(
  bfs::path where,
  infinit::model::Address node_id,
  bool enable_async,
  int cache_size,
  infinit::cryptography::rsa::KeyPair& kp)
{
  std::unique_ptr<infinit::storage::Storage> s;
  boost::filesystem::create_directories(where / "store");
  boost::filesystem::create_directories(where / "async");
  s.reset(new infinit::storage::Filesystem(where / "store"));
  infinit::model::doughnut::Passport passport(kp.K(), "testnet", kp);
  infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
    [&] (infinit::model::doughnut::Doughnut& dht)
        -> std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
        {
           auto backend = elle::make_unique<
             infinit::model::doughnut::consensus::Paxos>(dht, 1);
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
    node_id,
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

void
cleanup(bfs::path where)
{
  boost::filesystem::remove_all(where);
}

void
writefile(std::unique_ptr<reactor::filesystem::FileSystem>& fs,
               std::string const& name,
               std::string const& content)
{
  auto handle = fs->path("/")->child(name)->create(O_RDWR, 0666 | S_IFREG);
  handle->write(elle::WeakBuffer((char*)content.data(), content.size()),
                content.size(), 0);
  handle->close();
  handle.reset();
}

int
root_count(std::unique_ptr<reactor::filesystem::FileSystem>& fs)
{
  int count = 0;
  fs->path("/")->list_directory([&](std::string const&, struct stat*)
    { ++count;});
  return count;
}

ELLE_TEST_SCHEDULED(async_cache)
{
  auto node_id = infinit::model::Address::random(0);
  auto path = bfs::temp_directory_path() / bfs::unique_path();
  auto kp = infinit::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::os::setenv("INFINIT_HOME", path.string(), true);
  elle::os::setenv("INFINIT_PREFETCH_THREADS", "0", true);
  elle::SafeFinally cleanup_path([&] {
      boost::filesystem::remove_all(path);
  });
  auto fs = make(path, node_id, true, 10, kp);
  auto root = fs->path("/");
  auto handle = root->child("foo")->create(O_RDWR, 0666 | S_IFREG);
  ELLE_DEBUG("test that re-instantiating everything works")
  {
    handle->write(elle::WeakBuffer((char*)"bar", 3), 3, 0);
    handle->close();
    handle.reset();
    BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
    root.reset();
    fs.reset();
    fs = make(path, node_id, true, 10, kp);
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
  }

  ELLE_LOG("basic journal")
  {
    elle::os::setenv("INFINIT_ASYNC_NOPOP", "1", 1);
    fs.reset();
    fs = make(path, node_id, true, 10, kp);
    handle = fs->path("/")->child("foo2")->create(O_RDWR, 0666 | S_IFREG);
    handle->write(elle::WeakBuffer((char*)"bar", 3), 3, 0);
    handle->close();
    handle = fs->path("/")->child("foo")->open(O_RDWR, 0666);
    handle->write(elle::WeakBuffer((char*)"bar", 3), 3, 3);
    handle->close();
    handle.reset();
    fs->path("/")->child("dir")->mkdir(777 | S_IFDIR);
    elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
    fs.reset();
    fs = make(path, node_id, true, 10, kp);
    BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
    handle = fs->path("/")->child("foo")->open(O_RDWR, 0666);
    char buf[10] = {0};
    handle->read(elle::WeakBuffer(buf, 6), 6, 0);
    handle->close();
    handle.reset();
    buf[7] = 0;
    BOOST_CHECK_EQUAL(std::string(buf), "barbar");
    int count = 0;
    fs->path("/")->list_directory([&](std::string const&, struct stat*)
                                  { ++count;});
    BOOST_CHECK_EQUAL(count, 3);
    fs.reset();
  }

  ELLE_LOG("conflict dir")
  {
    elle::os::setenv("INFINIT_ASYNC_NOPOP", "1", 1);
    fs = make(path, node_id, true, 10, kp);
    // queue a file creation
    writefile(fs, "file", "foo");
    fs.reset();
    // create another file in the same dir
    fs = make(path, node_id, false, 0, kp);
    writefile(fs, "file2", "bar");
    fs.reset();
    elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
    // restart with async which will dequeue
    fs = make(path, node_id, true, 10, kp);
    BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
    BOOST_CHECK_EQUAL(root_count(fs), 5);
    fs.reset();
  }

  ELLE_LOG("conflict dir 2")
  {
    elle::os::setenv("INFINIT_ASYNC_NOPOP", "1", 1);
    fs = make(path, node_id, true, 10, kp);
    // queue a file creation
    writefile(fs, "samefile", "foo");
    fs.reset();
    // create same file in the same dir
    fs = make(path, node_id, false, 0, kp);
    writefile(fs, "samefile", "bar");
    fs.reset();
    elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
    // restart with async which will dequeue
    fs = make(path, node_id, true, 10, kp);
    BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
    BOOST_CHECK_EQUAL(root_count(fs), 6);
    fs.reset();
  }

  ELLE_LOG("conflict file")
  {
    elle::os::setenv("INFINIT_ASYNC_NOPOP", "1", 1);
    fs = make(path, node_id, true, 10, kp);
    // queue a file creation
    writefile(fs, "samefile", "foo");
    fs.reset();
    // create same file in the same dir
    fs = make(path, node_id, false, 0, kp);
    writefile(fs, "samefile", "bar");
    fs.reset();
    elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
    // restart with async which will dequeue
    fs = make(path, node_id, true, 10, kp);
    BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
    BOOST_CHECK_EQUAL(root_count(fs), 6);
    struct stat st;
    fs->path("/")->child("samefile")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_size, 3);
    fs.reset();
  }

  ELLE_LOG("ACL conflict")
  {
    auto kp2 = infinit::cryptography::rsa::keypair::generate(1024);
    auto pub2 = elle::serialization::json::serialize(kp2.K());
    elle::os::setenv("INFINIT_ASYNC_NOPOP", "1", 1);
    fs = make(path, node_id, true, 10, kp);
    // queue a attr change
    fs->path("/")->child("samefile")->setxattr("infinit.auth.setrw",
                                               std::string((const char*)pub2.contents(), pub2.size()), 0);
    fs.reset();
    // write same file in the same dir
    fs = make(path, node_id, false, 0, kp);
    writefile(fs, "samefile", "bar");
    fs.reset();
    elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
    // restart with async which will dequeue
    fs = make(path, node_id, true, 10, kp);
    BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
    BOOST_CHECK_EQUAL(root_count(fs), 6);
    auto auth = fs->path("/")->child("samefile")->getxattr("user.infinit.auth");
    std::stringstream sauth(auth);
    auto jauth = elle::json::read(sauth);
    BOOST_CHECK_EQUAL(boost::any_cast<elle::json::Array>(jauth).size(), 2);
    fs.reset();
  }
}

ELLE_TEST_SCHEDULED(async_groups)
{
  auto node_id = infinit::model::Address::random(0);
  auto path = bfs::temp_directory_path() / bfs::unique_path();
  auto kp = infinit::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::os::setenv("INFINIT_HOME", path.string(), true);
  elle::os::setenv("INFINIT_PREFETCH_THREADS", "0", true);
  elle::SafeFinally cleanup_path([&] {
      boost::filesystem::remove_all(path);
  });

  // Create group
  auto fs = make(path, node_id, false, 10, kp);
  auto dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
    dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
      ->block_store().get());
  {
    infinit::model::doughnut::Group g(*dn, "test_group");
    g.create();
    g.current_public_key();
    g.group_keys();
  }
  fs.reset();

  // stash agroup update
  elle::os::setenv("INFINIT_ASYNC_NOPOP", "1", 1);
  fs = make(path, node_id, true, 10, kp);
  auto root = fs->path("/");
  dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
    dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
      ->block_store().get());
  {
    infinit::model::doughnut::Group g(*dn, "test_group");
    g.remove_member(elle::serialization::json::serialize(kp.public_key()));
    g.current_public_key();
    g.group_keys();
  }
  {
    infinit::model::doughnut::Group g(*dn, "test_group");
    g.current_public_key();
    g.group_keys();
    g.list_members();
  }
  fs.reset();

  // push some group update
  fs = make(path, node_id, false, 10, kp);
  dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
    dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
      ->block_store().get());
  {
    infinit::model::doughnut::Group g(*dn, "test_group");
    g.remove_member(elle::serialization::json::serialize(kp.public_key()));
    g.current_public_key();
    g.group_keys();
  }
  fs.reset();

  // unstash
  elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
  fs = make(path, node_id, true, 10, kp);
  dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
    dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
      ->block_store().get());
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  {
    infinit::model::doughnut::Group g(*dn, "test_group");
    g.current_public_key();
    g.group_keys();
  }
  BOOST_CHECK(true);
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(async_cache), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(async_groups), 0, valgrind(10));
}
