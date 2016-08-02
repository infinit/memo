#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <boost/filesystem.hpp>

#include <elle/os/environ.hh>
#include <elle/test.hh>

#include <reactor/filesystem.hh>

#include <infinit/filesystem/filesystem.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("infinit.model.doughnut.bandwidth-test");

ELLE_TEST_SCHEDULED(bazillion_small_files)
{
  auto path = boost::filesystem::temp_directory_path()
    / boost::filesystem::unique_path();
  elle::os::setenv("INFINIT_HOME", path.string(), true);
  elle::SafeFinally cleanup_path([&] {
      boost::filesystem::remove_all(path);
  });
  auto k = infinit::cryptography::rsa::keypair::generate(512);
  DHT server_a(owner = k);
  DHT client(keys = k, storage = nullptr);
  client.overlay->connect(*server_a.overlay);
  auto fs = elle::make_unique<infinit::filesystem::FileSystem>(
    "test/bandwidth", client.dht,
    infinit::filesystem::allow_root_creation = true);
  reactor::filesystem::FileSystem driver(std::move(fs), true);
  auto root = driver.path("/");
  int const max = std::stoi(elle::os::getenv("ITERATIONS", "100"));
  auto& storage =
    dynamic_cast<infinit::storage::Memory&>(*server_a.dht->local()->storage());
  boost::optional<double> resident;
  for (int i = 0; i < max; ++i)
  {
    ELLE_LOG_SCOPE("%4s / %s\n", i, max);
    auto file = root->child(elle::sprintf("%04s", i));
    auto handle = file->create(O_RDWR, 0666 | S_IFREG);
    elle::Buffer contents(100 * 1024);
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
  suite.add(BOOST_TEST_CASE(bazillion_small_files), 0, valgrind(100));
}
