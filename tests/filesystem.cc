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
  std::unique_ptr<infinit::model::Model> model;
  reactor::Thread t(sched, "fs", [&] {
    if (!elle::os::getenv("STORAGE_MEMORY", "").empty())
      storage = new infinit::storage::Memory();
    else
      storage = new infinit::storage::Filesystem(store);
    model = elle::make_unique<infinit::model::faith::Faith>(
      std::unique_ptr<infinit::storage::Storage>(storage));
    std::unique_ptr<ifs::FileSystem> ops = elle::make_unique<ifs::FileSystem>(
      std::move(model));
    fs = new reactor::filesystem::FileSystem(std::move(ops), true);
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

static void write(boost::filesystem::path const& where, std::string const& what)
{
  boost::filesystem::ofstream ofs(where);
  ofs << what;
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
  bfs::remove(mount / "test");
  bfs::remove(mount / "test2");
  //holes
  int fd = open((mount / "test").string().c_str(), O_RDWR|O_CREAT, 0644);
  if (fd < 0)
    perror("open");
  ELLE_ENFORCE_EQ(write(fd, "foo", 3), 3);
  lseek(fd, 10, SEEK_CUR);
  ELLE_ENFORCE_EQ(write(fd, "foo", 3), 3);
  close(fd);
  {
    bfs::ifstream ifs(mount / "test");
    char buffer[20];
    ifs.read(buffer, 20);
    BOOST_CHECK_EQUAL(ifs.gcount(), 16);
    char expect[] = {'f','o','o',0,0,0,0,0,0,0,0,0,0,'f','o','o'};
    BOOST_CHECK_EQUAL(std::string(buffer, buffer + 16), std::string(expect, expect + 16));
  }
  bfs::remove(mount / "test");
  // use after unlink
  fd = open((mount / "test").string().c_str(), O_RDWR|O_CREAT, 0644);
  if (fd < 0)
    perror("open");
  ELLE_ENFORCE_EQ(write(fd, "foo", 3), 3);
  bfs::remove(mount / "test");
  int res = write(fd, "foo", 3);
  BOOST_CHECK_EQUAL(res, 3);
  lseek(fd, 0, SEEK_SET);
  char buf[7] = {0};
  res = read(fd, buf, 6);
  BOOST_CHECK_EQUAL(res, 6);
  BOOST_CHECK_EQUAL(buf, "foofoo");
  close(fd);
  BOOST_CHECK_EQUAL(directory_count(mount), 0);

  //rename
  {
    boost::filesystem::ofstream ofs(mount / "test");
    ofs << "Test";
  }
  bfs::rename(mount / "test", mount / "test2");
  BOOST_CHECK_EQUAL(read(mount / "test2"), "Test");
  write(mount / "test3", "foo");
  bfs::rename(mount / "test2", mount / "test3");
  BOOST_CHECK_EQUAL(read(mount / "test3"), "Test");
  BOOST_CHECK_EQUAL(directory_count(mount), 1);
  bfs::create_directory(mount / "dir");
  write(mount / "dir" / "foo", "bar");
  bfs::rename(mount / "test3", mount / "dir", erc);
  BOOST_CHECK_EQUAL(!!erc, true);
  bfs::rename(mount / "dir", mount / "dir2");
  bfs::remove(mount / "dir2", erc);
  BOOST_CHECK_EQUAL(!!erc, true);
  bfs::rename(mount / "dir2" / "foo", mount / "foo");
  bfs::remove(mount / "dir2");
  bfs::remove(mount / "foo");

  // cross-block
  fd = open((mount / "foo").string().c_str(), O_RDWR|O_CREAT, 0644);
  BOOST_CHECK_GE(fd, 0);
  lseek(fd, 1024*1024 - 10, SEEK_SET);
  const char* data = "abcdefghijklmnopqrstuvwxyz";
  res = write(fd, data, strlen(data));
  BOOST_CHECK_EQUAL(res, strlen(data));
  close(fd);
  stat((mount / "foo").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 1024*1024 - 10 + 26);
  char output[36];
  fd = open((mount / "foo").string().c_str(), O_RDONLY);
  BOOST_CHECK_GE(fd, 0);
  lseek(fd, 1024*1024 - 15, SEEK_SET);
  res = read(fd, output, 36);
  BOOST_CHECK_EQUAL(31, res);
  BOOST_CHECK_EQUAL(std::string(output, output+31),
                    std::string(5, 0) + data);
  close(fd);

  // link/unlink
  fd = open((mount / "u").string().c_str(), O_RDWR|O_CREAT, 0644);
  ::close(fd);
  bfs::remove(mount / "u");

  // multiple opens
  {
    boost::filesystem::ofstream ofs(mount / "test");
    ofs << "Test";
    boost::filesystem::ofstream ofs2(mount / "test");
  }
  BOOST_CHECK_EQUAL(read(mount / "test"), "Test");
  bfs::remove(mount / "test");
  {
    boost::filesystem::ofstream ofs(mount / "test");
    ofs << "Test";
    {
      boost::filesystem::ofstream ofs2(mount / "test");
    }
    ofs << "Test";
  }
  BOOST_CHECK_EQUAL(read(mount / "test"), "TestTest");
  bfs::remove(mount / "test");

  // randomized manyops
  std::default_random_engine gen;
  std::uniform_int_distribution<>dist(0, 255);
  {
    boost::filesystem::ofstream ofs(mount / "tbig");
    for (int i=0; i<10000000; ++i)
      ofs.put(dist(gen));
  }
  BOOST_CHECK_EQUAL(boost::filesystem::file_size(mount / "tbig"), 10000000);
  std::uniform_int_distribution<>dist2(0, 9999999);
  for (int i=0; i < 200; ++i)
  {
    int fd = open((mount / "foo").string().c_str(), O_RDWR);
    for (int i=0; i < 5; ++i)
    {
      lseek(fd, dist2(gen), SEEK_SET);
      unsigned char c = dist(gen);
      write(fd, &c, 1);
    }
    close(fd);
  }
  BOOST_CHECK_EQUAL(boost::filesystem::file_size(mount / "tbig"), 10000000);
  bfs::remove(mount / "tbig");
}


ELLE_TEST_SUITE()
{
  boost::unit_test::test_suite* filesystem = BOOST_TEST_SUITE("filesystem");
  boost::unit_test::framework::master_test_suite().add(filesystem);
  filesystem->add(BOOST_TEST_CASE(test_basic), 0, 50);
}
