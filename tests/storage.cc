#include <elle/filesystem/TemporaryDirectory.hh>
#include <elle/test.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/MissingKey.hh>
#include <infinit/storage/Storage.hh>

ELLE_LOG_COMPONENT("tests.storage");

static
void
tests(infinit::storage::Storage& storage)
{
  infinit::storage::Key::Value v1 = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
  };
  infinit::storage::Key k1(&v1[0]);
  char const* data1 = "the grey";
  storage.set(k1, elle::Buffer(data1, strlen(data1)));
  BOOST_CHECK_EQUAL(storage.get(k1), data1);
  char const* data2 = "the white";
  storage.set(k1, elle::Buffer(data2, strlen(data2)), false, true);
  BOOST_CHECK_EQUAL(storage.get(k1), data2);
  BOOST_CHECK_THROW(storage.set(k1, elle::Buffer()),
                    infinit::storage::Collision);
  BOOST_CHECK_EQUAL(storage.get(k1), data2);
  infinit::storage::Key::Value v2 = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2
  };
  infinit::storage::Key k2(&v2[0]);
  BOOST_CHECK_THROW(storage.get(k2), infinit::storage::MissingKey);
  BOOST_CHECK_THROW(storage.set(k2, elle::Buffer(), false, true),
                    infinit::storage::MissingKey);
  BOOST_CHECK_THROW(storage.erase(k2), infinit::storage::MissingKey);
  storage.erase(k1);
  BOOST_CHECK_THROW(storage.get(k1), infinit::storage::MissingKey);
}

static
void
tests_capacity(infinit::storage::Storage& storage, int64_t capacity)
{
  infinit::storage::Key::Value v1 = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
  };
  infinit::storage::Key k1(&v1[0]);
  BOOST_CHECK_EQUAL(storage.capacity(), capacity);
  BOOST_CHECK_EQUAL(storage.usage(), 0);

  char* data = new char[capacity / 2];
  for (int i = 0; i < capacity / 2; ++i)
    data[i] = 'n';
  data[capacity / 2 - 1] = '\0';
  ELLE_LOG("set data to storage");
  storage.set(k1, elle::Buffer(data, capacity / 2));
  BOOST_CHECK_EQUAL(storage.usage(), capacity / 2);
  ELLE_LOG("erase data from storage");
  storage.erase(k1);
  BOOST_CHECK_EQUAL(storage.usage(), 0);
  delete[] data;
  data = new char[capacity];
  for (int i = 0; i < capacity; ++i)
    data[i] = 'u';
  data[capacity - 1] = '\0';
  ELLE_LOG("set data to storage");
  storage.set(k1, elle::Buffer(data, capacity));
  BOOST_CHECK_EQUAL(storage.usage(), capacity);
  ELLE_LOG("erase data from storage");
  storage.erase(k1);
  BOOST_CHECK_EQUAL(storage.usage(), 0);
  delete[] data;
}

static
void
memory()
{
  infinit::storage::Memory storage;
  tests(storage);
}


static
void
filesystem()
{
  elle::filesystem::TemporaryDirectory d;
  infinit::storage::Filesystem storage(d.path());
  tests(storage);
}

static
void
filesystem_small_capacity()
{
  ELLE_TRACE("starting filesystem_small_capacity");
  elle::filesystem::TemporaryDirectory d;
  int64_t size = 7;
  infinit::storage::Filesystem storage(d.path(), size);
  tests_capacity(storage, size);
}

static
void
filesystem_large_capacity()
{
  ELLE_TRACE("starting filesystem_large_capacity");
  elle::filesystem::TemporaryDirectory d;
  int64_t size = 42;
  infinit::storage::Filesystem storage(d.path(), size);
  tests_capacity(storage, size);
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(filesystem));
  suite.add(BOOST_TEST_CASE(filesystem_small_capacity));
  suite.add(BOOST_TEST_CASE(filesystem_large_capacity));
  suite.add(BOOST_TEST_CASE(memory));
}
