#include <elle/test.hh>
#include <elle/json/json.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/json.hh>

#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/Group.hh>
#include <infinit/overlay/Kalimero.hh>
#include <infinit/silo/Filesystem.hh>
#include <infinit/silo/Memory.hh>
#include <infinit/silo/Silo.hh>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>

#ifdef INFINIT_WINDOWS
# undef stat
#endif

ELLE_LOG_COMPONENT("test");

namespace ifs = infinit::filesystem;
namespace rfs = elle::reactor::filesystem;
namespace bfs = boost::filesystem;

std::unique_ptr<elle::reactor::filesystem::FileSystem>
make(bfs::path where,
     infinit::model::Address node_id,
     bool enable_async,
     int cache_size,
     elle::cryptography::rsa::KeyPair const& kp)
{
  bfs::create_directories(where / "store");
  bfs::create_directories(where / "async");
  auto s = std::unique_ptr<infinit::silo::Silo>{
    new infinit::silo::Filesystem(where / "store")};
  infinit::model::doughnut::Passport passport(kp.K(), "testnet", kp);
  infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
    [&] (infinit::model::doughnut::Doughnut& dht)
        -> std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
        {
           auto backend = std::make_unique<
             infinit::model::doughnut::consensus::Paxos>(dht, 1);
           if (!enable_async)
             return std::move(backend);
           auto async = std::make_unique<
             infinit::model::doughnut::consensus::Async>(std::move(backend),
               where / "async", cache_size);
           return std::move(async);
        };
  infinit::model::doughnut::Doughnut::OverlayBuilder overlay =
        [=] (infinit::model::doughnut::Doughnut& dht,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          return std::make_unique<infinit::overlay::Kalimero>(&dht, local);
        };
  auto dn = std::make_shared<infinit::model::doughnut::Doughnut>(
    node_id,
    std::make_shared<elle::cryptography::rsa::KeyPair>(kp),
    kp.public_key(),
    passport,
    consensus,
    overlay,
    boost::optional<int>(),
    boost::optional<boost::asio::ip::address>(),
    std::move(s));
  auto ops = std::make_unique<infinit::filesystem::FileSystem>(
    "volume", dn, infinit::filesystem::allow_root_creation = true);
  auto fs = std::make_unique<elle::reactor::filesystem::FileSystem>(
    std::move(ops), true);
  return fs;
}

void
cleanup(bfs::path where)
{
  bfs::remove_all(where);
}

void
writefile(std::unique_ptr<elle::reactor::filesystem::FileSystem>& fs,
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
root_count(std::unique_ptr<elle::reactor::filesystem::FileSystem>& fs)
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
  auto kp = elle::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::os::setenv("INFINIT_HOME", path.string(), true);
  elle::os::setenv("INFINIT_PREFETCH_THREADS", "0", true);
  elle::SafeFinally cleanup_path([&] {
      bfs::remove_all(path);
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
    BOOST_CHECK_EQUAL(count, 3);
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
    BOOST_CHECK_EQUAL(count, 5);
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
    BOOST_CHECK_EQUAL(root_count(fs), 7);
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
    BOOST_CHECK_EQUAL(root_count(fs), 8);
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
    BOOST_CHECK_EQUAL(root_count(fs), 8);
    struct stat st;
    fs->path("/")->child("samefile")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_size, 3);
    fs.reset();
  }

  ELLE_LOG("ACL conflict")
  {
    auto kp2 = elle::cryptography::rsa::keypair::generate(1024);
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
    BOOST_CHECK_EQUAL(root_count(fs), 8);
    auto auth = fs->path("/")->child("samefile")->getxattr("user.infinit.auth");
    std::stringstream sauth(auth);
    auto jauth = elle::json::read(sauth);
    BOOST_CHECK_EQUAL(boost::any_cast<elle::json::Array>(jauth).size(), 2);
    fs.reset();
  }
}

ELLE_TEST_SCHEDULED(async_groups)
{
  auto const node_id = infinit::model::Address::random(0);
  auto const path = bfs::temp_directory_path() / bfs::unique_path();
  auto const kp = elle::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::os::setenv("INFINIT_HOME", path.string());
  elle::os::setenv("INFINIT_PREFETCH_THREADS", "0");
  elle::SafeFinally cleanup_path([&] {
      bfs::remove_all(path);
  });

  // Create group
  {
    auto fs = make(path, node_id, false, 10, kp);
    auto dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
      dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
        ->block_store().get());
    {
      auto&& g = infinit::model::doughnut::Group(*dn, "test_group");
      g.create();
      g.current_public_key();
      g.group_keys();
    }
  }

  // stash agroup update
  {
    elle::os::setenv("INFINIT_ASYNC_NOPOP", "1");
    auto const fs = make(path, node_id, true, 10, kp);
    auto const root = fs->path("/");
    auto const dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
      dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
        ->block_store().get());
    {
      auto&& g = infinit::model::doughnut::Group(*dn, "test_group");
      g.remove_member(elle::serialization::json::serialize(kp.public_key()));
      g.current_public_key();
      g.group_keys();
    }
    {
      auto&& g = infinit::model::doughnut::Group(*dn, "test_group");
      g.current_public_key();
      g.group_keys();
      g.list_members();
    }
  }

  // push some group update
  {
    auto const fs = make(path, node_id, false, 10, kp);
    auto const dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
      dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
        ->block_store().get());
    {
      auto&& g = infinit::model::doughnut::Group(*dn, "test_group");
      g.remove_member(elle::serialization::json::serialize(kp.public_key()));
      g.current_public_key();
      g.group_keys();
    }
  }

  // unstash
  {
    elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
    auto const fs = make(path, node_id, true, 10, kp);
    auto const dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
      dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
        ->block_store().get());
    BOOST_TEST(fs->path("/")->getxattr("user.infinit.sync") == "ok");
    {
      auto&& g = infinit::model::doughnut::Group(*dn, "test_group");
      g.current_public_key();
      g.group_keys();
    }
    BOOST_CHECK(true);
  }
}

infinit::model::doughnut::consensus::Async*
async(std::unique_ptr<elle::reactor::filesystem::FileSystem>& fs)
{
  auto dn = dynamic_cast<infinit::model::doughnut::Doughnut*>(
    dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get())
    ->block_store().get());
  return dynamic_cast<infinit::model::doughnut::consensus::Async*>(dn->consensus().get());
}

ELLE_TEST_SCHEDULED(async_squash2)
{
  auto node_id = infinit::model::Address::random(0);
  auto path = bfs::temp_directory_path() / bfs::unique_path();
  auto kp = elle::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::os::setenv("INFINIT_HOME", path.string());
  elle::os::setenv("INFINIT_PREFETCH_THREADS", "0");
  elle::SafeFinally cleanup_path([&] {
      bfs::remove_all(path);
  });

  // squash creating directories
  elle::os::setenv("INFINIT_ASYNC_NOPOP", "1");
  auto fs = make(path, node_id, true, 100, kp);
  for (int i=0; i<10; ++i)
    fs->path(elle::sprintf("/f%s", i))->mkdir(0600);
  auto a = async(fs);
  a->print_queue();
  fs.reset();
  elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
  fs = make(path, node_id, true, 100, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  a = async(fs);
  BOOST_CHECK_LE(a->processed_op_count(), 14); // one write per dir
  fs.reset();

  // squash creating files
  elle::os::setenv("INFINIT_ASYNC_NOPOP", "1");
  fs = make(path, node_id, true, 100, kp);
  for (int i=0; i<10; ++i)
    writefile(fs, elle::sprintf("file%s", i), "foo");
  a = async(fs);
  a->print_queue();
  fs.reset();
  elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
  fs = make(path, node_id, true, 100, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  a = async(fs);
  BOOST_CHECK_LE(a->processed_op_count(), 24); // 2 writes per file
  fs.reset();

  // squash other op barrier
  elle::os::setenv("INFINIT_ASYNC_NOPOP", "1");
  fs = make(path, node_id, true, 100, kp);
  for (int i=0; i<10; ++i)
  {
    fs->path(elle::sprintf("/filefile%s", i))->create(O_CREAT|O_RDWR, 0600);
    if (i%2)
      fs->path("/")->chmod(0600);
  }
  a = async(fs);
  a->print_queue();
  fs.reset();
  elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
  fs = make(path, node_id, true, 100, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  a = async(fs);
  BOOST_CHECK_LE(a->processed_op_count(), 30);
  BOOST_CHECK_GE(a->processed_op_count(), 25);
  fs.reset();
}

ELLE_TEST_SCHEDULED(async_squash)
{
  auto node_id = infinit::model::Address::random(0);
  auto path = bfs::temp_directory_path() / bfs::unique_path();
  auto kp = elle::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::os::setenv("INFINIT_HOME", path.string());
  elle::os::setenv("INFINIT_PREFETCH_THREADS", "0");
  elle::SafeFinally cleanup_path([&] {
      bfs::remove_all(path);
  });
  elle::os::setenv("INFINIT_ASYNC_NOPOP", "1");
  auto fs = make(path, node_id, true, 100, kp);
  fs->path("/d1")->mkdir(0600);
  fs->path("/d2")->mkdir(0600);
  writefile(fs, "f3", "foo");
  writefile(fs, "f4", "foo");
  BOOST_CHECK_EQUAL(root_count(fs), 6);

  fs.reset();
  elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
  fs = make(path, node_id, true, 100, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  BOOST_CHECK_EQUAL(root_count(fs), 6);

  fs.reset();
  elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
  fs = make(path, node_id, false, 0, kp);
  BOOST_CHECK_EQUAL(root_count(fs), 6);
}

ELLE_TEST_SCHEDULED(async_squash_conflict)
{
  auto node_id = infinit::model::Address::random(0);
  auto path = bfs::temp_directory_path() / bfs::unique_path();
  auto kp = elle::cryptography::rsa::keypair::generate(1024);
  ELLE_LOG("root path: %s", path);
  elle::os::setenv("INFINIT_HOME", path.string());
  elle::os::setenv("INFINIT_PREFETCH_THREADS", "0");
  elle::SafeFinally cleanup_path([&] {
      bfs::remove_all(path);
  });

  auto fs = make(path, node_id, false, 0, kp);
  BOOST_CHECK_EQUAL(root_count(fs), 2);
  fs.reset();

  elle::os::setenv("INFINIT_ASYNC_NOPOP", "1");
  fs = make(path, node_id, true, 100, kp);
  fs->path("/d1")->mkdir(0600);
  fs->path("/d2")->mkdir(0600);
  fs->path("/dr")->mkdir(0600);
  writefile(fs, "f3", "foo");
  writefile(fs, "fr", "foo");
  writefile(fs, "f4", "foo");
  fs->path("/dr")->rmdir();
  fs->path("/fr")->unlink();
  BOOST_CHECK_EQUAL(root_count(fs), 6);

  fs.reset();
  fs = make(path, node_id, false, 0, kp);
  writefile(fs, "fc", "foo");
  BOOST_CHECK_EQUAL(root_count(fs), 3);

  fs.reset();
  elle::os::unsetenv("INFINIT_ASYNC_NOPOP");
  fs = make(path, node_id, true, 100, kp);
  BOOST_CHECK_EQUAL(fs->path("/")->getxattr("user.infinit.sync"), "ok");
  // Async operation so check multiple times to avoid timing issues.
  bool are_7 = false;
  for (int i = 0; i < 10; i++)
  {
    if (root_count(fs) == 7)
    {
      are_7 = true;
      break;
    }
    elle::reactor::sleep(100_ms);
  }
  BOOST_CHECK(are_7);

  fs.reset();
  fs = make(path, node_id, false, 0, kp);
  BOOST_CHECK_EQUAL(root_count(fs), 7);
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(async_cache), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(async_groups), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(async_squash), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(async_squash2), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(async_squash_conflict), 0, valgrind(10));
}
