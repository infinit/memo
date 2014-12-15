#include <boost/filesystem/fstream.hpp>

#include <elle/os/environ.hh>

#include <elle/test.hh>

#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>

#include <infinit/storage/Storage.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Filesystem.hh>

#include <infinit/model/faith/Faith.hh>

namespace ifs = infinit::filesystem;
namespace rfs = reactor::filesystem;

infinit::storage::Storage* storage;
reactor::filesystem::FileSystem* fs;
reactor::Scheduler sched;
static void sig_int()
{
  fs->unmount();
}

static int directory_count(boost::filesystem::path const& p)
{
  boost::filesystem::directory_iterator d(p);
  int s=0;
  while (d!= boost::filesystem::directory_iterator())
  {
    ++s; ++d;
  }
  return s;
}

static void run_filesystem(std::string const& store, std::string const& mountpoint)
{
  auto tmp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

  reactor::Thread t(sched, "fs", [&] {
    if (!elle::os::getenv("STORAGE_MEMORY", "").empty())
      storage = new infinit::storage::Memory();
    else
      storage = new infinit::storage::Filesystem(store);
    auto model = elle::make_unique<infinit::model::faith::Faith>(*storage);

    std::unique_ptr<ifs::FileSystem> ops = elle::make_unique<ifs::FileSystem>(
      "", std::move(model));
    ifs::FileSystem* ops_ptr = ops.get();
    fs = new reactor::filesystem::FileSystem(std::move(ops), true);
    ops_ptr->fs(fs);
    fs->mount(mountpoint, {"", "-o", "big_writes"}); // {"", "-d" /*, "-o", "use_ino"*/});
  });
  sched.run();
}

static std::string read(boost::filesystem::path const& where)
{
  std::string text;
  boost::filesystem::ifstream ifs(where);
  ifs >> text;
  return text;
}

void test_basic()
{
  namespace bfs = boost::filesystem;
  auto store = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  auto mount = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  boost::filesystem::create_directories(mount);
  boost::filesystem::create_directories(store);
  std::thread t([&] {run_filesystem(store.string(), mount.string());});
  usleep(500000);
  elle::SafeFinally remover([&] {
      reactor::Thread th(sched, "unmount", [&] { fs->unmount();});
      t.join();
      boost::filesystem::remove_all(store);
      boost::filesystem::remove_all(mount);
  });
  {
    boost::filesystem::ofstream ofs(mount / "test");
    ofs << "Test";
  }
  std::string text;
  BOOST_CHECK_EQUAL(directory_count(mount), 1);
  {
    bfs::ifstream ifs(mount / "test");
    ifs >> text;
  }
  BOOST_CHECK_EQUAL(text, "Test");
  {
    bfs::ofstream ofs(mount / "test", std::ofstream::out|std::ofstream::ate|std::ofstream::app);
    ofs << "coin";
  }
  BOOST_CHECK_EQUAL(directory_count(mount), 1);
  {
    bfs::ifstream ifs(mount / "test");
    ifs >> text;
  }
  BOOST_CHECK_EQUAL(text, "Testcoin");
  BOOST_CHECK_EQUAL(bfs::file_size(mount/"test"), 8);
  bfs::remove(mount / "test");
  BOOST_CHECK_EQUAL(directory_count(mount), 0);
  boost::system::error_code erc;
  bfs::file_size(mount / "foo", erc);
  BOOST_CHECK_EQUAL(true, !!erc);

  // hardlink
  {
    bfs::ofstream ofs(mount / "test");
    ofs << "Test";
  }
  bfs::create_hard_link(mount / "test", mount / "test2");
  {
    bfs::ofstream ofs(mount / "test2", std::ofstream::out|std::ofstream::ate|std::ofstream::app);
    ofs << "coinB";
    ofs.close();
  }
  usleep(500000);
  struct stat st;
  stat((mount / "test").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 9);
  stat((mount / "test2").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 9);
  text = read(mount / "test2");
  BOOST_CHECK_EQUAL(text, "TestcoinB");
  text = read(mount / "test");
  BOOST_CHECK_EQUAL(text, "TestcoinB");

  {
    bfs::ofstream ofs(mount / "test", std::ofstream::out|std::ofstream::ate|std::ofstream::app);
    ofs << "coinA";
  }
  usleep(500000);
  stat((mount / "test").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 14);
  stat((mount / "test2").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 14);
  text = read(mount / "test");
  BOOST_CHECK_EQUAL(text, "TestcoinBcoinA");
  text = read(mount / "test2");
  BOOST_CHECK_EQUAL(text, "TestcoinBcoinA");
}


ELLE_TEST_SUITE()
{
  boost::unit_test::test_suite* filesystem = BOOST_TEST_SUITE("filesystem");
  boost::unit_test::framework::master_test_suite().add(filesystem);
  filesystem->add(BOOST_TEST_CASE(test_basic), 0, 50);
}

