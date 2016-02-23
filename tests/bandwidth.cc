#include <elle/os/environ.hh>
#include <elle/test.hh>

#include <reactor/filesystem.hh>

#include <infinit/filesystem/filesystem.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("infinit.model.doughnut.bandwidth-test");

ELLE_TEST_SCHEDULED(bazillion_small_files)
{
  DHT server_a;
  DHT server_b;
  DHT server_c;
  server_a.overlay->connect(*server_b.overlay);
  server_a.overlay->connect(*server_c.overlay);
  server_b.overlay->connect(*server_c.overlay);
  DHT client(storage = nullptr);
  client.overlay->connect(*server_a.overlay);
  client.overlay->connect(*server_b.overlay);
  client.overlay->connect(*server_c.overlay);
  auto fs = elle::make_unique<infinit::filesystem::FileSystem>(
    "test/bandwidth", client.dht);
  reactor::filesystem::FileSystem driver(std::move(fs), true);
  auto root = driver.path("/");
  int const max = std::stoi(elle::os::getenv("ITERATIONS", "100"));
  for (int i = 0; i < max; ++i)
  {
    elle::printf("%4s / %s\n", i, max);
    auto file = root->child(elle::sprintf("%04s", i));
    auto handle = file->create(O_RDWR, 0666 | S_IFREG);
    elle::Buffer contents(100 * 1024);
    memset(contents.mutable_contents(), 0xfd, contents.size());
    handle->write(contents, contents.size(), 0);
    handle->close();
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(bazillion_small_files), 0, valgrind(100));
}
