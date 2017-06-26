#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <boost/filesystem.hpp>

#include <elle/os/environ.hh>
#include <elle/test.hh>

#include <elle/reactor/filesystem.hh>

#include <memo/filesystem/filesystem.hh>

#include "DHT.hh"

namespace bfs = boost::filesystem;

ELLE_LOG_COMPONENT("memo.model.doughnut.bandwidth-test");

ELLE_TEST_SCHEDULED(bazillion_small_files)
{
  auto path = bfs::temp_directory_path() / bfs::unique_path();
  elle::os::setenv("MEMO_HOME", path.string());
  elle::SafeFinally cleanup_path([&] {
      bfs::remove_all(path);
  });
  auto const k = elle::cryptography::rsa::keypair::generate(512);
  auto server_a = DHT(owner = k);
  auto client = DHT(keys = k, storage = nullptr);
  client.overlay->connect(*server_a.overlay);
  auto fs = std::make_unique<memo::filesystem::FileSystem>(
    "test/bandwidth", client.dht,
    memo::filesystem::allow_root_creation = true);
  elle::reactor::filesystem::FileSystem driver(std::move(fs), true);
  auto root = driver.path("/");
  int const max = elle::os::getenv("ITERATIONS", 100);
  auto& storage =
    dynamic_cast<memo::silo::Memory&>(*server_a.dht->local()->storage());
  auto resident = boost::optional<double>{};
  for (int i = 0; i < max; ++i)
  {
    ELLE_LOG_SCOPE("%s / %s", i, max);
    auto file = root->child(elle::sprintf("%04s", i));
    auto handle = file->create(O_RDWR, 0666 | S_IFREG);
    auto contents = elle::Buffer(100 * 1024);
    memset(contents.mutable_contents(), 0xfd, contents.size());
    handle->write(contents, contents.size(), 0);
    handle->close();
    root->child(elle::sprintf("%04s", i))->unlink();
    if (!resident)
      resident = storage.size();
    else
      // Check storage space stays within 5%
      BOOST_CHECK_CLOSE(double(storage.size()), *resident, 5.);
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  // Takes 47s with Valgrind in Docker on a labtop, otherwise less than 2s.
  suite.add(BOOST_TEST_CASE(bazillion_small_files), 0, valgrind(20, 20));
}
