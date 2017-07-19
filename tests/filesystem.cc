#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef ELLE_WINDOWS
# include <sys/statvfs.h>
#endif

#ifdef ELLE_WINDOWS
# undef stat
#endif

#if defined ELLE_LINUX
# include <attr/xattr.h>
#elif defined ELLE_MACOS
# include <sys/xattr.h>
#endif

#include <cerrno>
#include <random>

#include <boost/filesystem/fstream.hpp>
#include <boost/range/irange.hpp>

#include <elle/UUID.hh>
#include <elle/Version.hh>
#include <elle/algorithm.hh>
#include <elle/format/base64.hh>
#include <elle/os/environ.hh>
#include <elle/random.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/system/Process.hh>
#include <elle/test.hh>
#include <elle/utils.hh>

#include <elle/reactor/scheduler.hh>

#include <memo/filesystem/filesystem.hh>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/Local.hh>
#include <memo/model/doughnut/Cache.hh>
#include <memo/model/doughnut/consensus/Paxos.hh>
#include <memo/model/faith/Faith.hh>
#include <memo/overlay/Stonehenge.hh>
#include <memo/silo/Filesystem.hh>
#include <memo/silo/Memory.hh>
#include <memo/silo/Silo.hh>
#include <memo/utility.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("test");

namespace ifs = memo::filesystem;
namespace rfs = elle::reactor::filesystem;
namespace bfs = boost::filesystem;


#ifdef ELLE_WINDOWS
# define O_CREAT _O_CREAT
# define O_RDWR _O_RDWR
# define O_EXCL _O_EXCL
# define S_IFREG _S_IFREG
#endif

namespace
{
  /// Please Valgrind and don't use buffers from random memory.
  template <int Size>
  void
  initialize(char (&buf)[Size])
  {
    for (int i = 0; i < Size; ++i)
      buf[i] = i % 391;
  }

  template<typename T>
  std::string
  serialize(T & t)
  {
    elle::Buffer buf;
    {
      elle::IOStream ios(buf.ostreambuf());
      elle::serialization::json::SerializerOut so(ios, false);
      so.serialize_forward(t);
    }
    return buf.string();
  }

  std::vector<memo::model::Address>
  get_fat(std::string const& attr)
  {
    std::stringstream input(attr);
    return elle::make_vector(boost::any_cast<elle::json::Array>(elle::json::read(input)),
                             [](auto const& entry)
                             {
                               return memo::model::Address::from_string(
                                 boost::any_cast<std::string>(entry));
                             });
  }

  class DHTs
  {
  public:
    template <typename ... Args>
    DHTs(int count)
     : DHTs(count, {})
    {}

    template <typename ... Args>
    DHTs(int count,
         boost::optional<elle::cryptography::rsa::KeyPair> kp,
         Args ... args)
      : owner_keys(kp? *kp : elle::cryptography::rsa::keypair::generate(512))
      , dhts()
    {
      pax = true;
      if (count < 0)
      {
        pax = false;
        count *= -1;
      }
      for (int i = 0; i < count; ++i)
      {
        this->dhts.emplace_back(paxos = pax,
                                owner = this->owner_keys,
                                std::forward<Args>(args) ...);
        for (int j = 0; j < i; ++j)
          this->dhts[j].overlay->connect(*this->dhts[i].overlay);
      }
    }

    struct Client
    {
      template<typename... Args>
      Client(std::string const& name, DHT dht, Args...args)
        : dht(std::move(dht))
        , fs(std::make_unique<elle::reactor::filesystem::FileSystem>(
               std::make_unique<memo::filesystem::FileSystem>(
                 name, this->dht.dht, ifs::allow_root_creation = true,
                 std::forward<Args>(args)...),
               true))
      {}

      DHT dht;
      std::unique_ptr<elle::reactor::filesystem::FileSystem> fs;
    };

    template<typename... Args>
    DHT
    dht(bool new_key,
           boost::optional<elle::cryptography::rsa::KeyPair> kp,
           Args... args)
    {
      auto k = kp ? *kp
      : new_key ? elle::cryptography::rsa::keypair::generate(512)
            : this->owner_keys;
      ELLE_LOG("new client with owner=%f key=%f", this->owner_keys.K(), k.K());
      DHT client(owner = this->owner_keys,
                 keys = k,
                 storage = nullptr,
                 dht::consensus_builder = no_cheat_consensus(pax),
                 paxos = pax,
                 std::forward<Args>(args) ...
                 );
      for (auto& dht: this->dhts)
        dht.overlay->connect(*client.overlay);
      return client;
    }

    template<typename... Args>
    Client
    client(bool new_key,
           boost::optional<elle::cryptography::rsa::KeyPair> kp,
           Args... args)
    {
      DHT client = dht(new_key, kp, std::forward<Args>(args)...);
      return Client("volume", std::move(client));
    }

    Client
    client(bool new_key = false)
    {
      return client(new_key, {});
    }

    elle::cryptography::rsa::KeyPair owner_keys;
    std::vector<DHT> dhts;
    bool pax;
  };
}

ELLE_TEST_SCHEDULED(write_truncate)
{
  auto servers = DHTs(1);
  auto client = servers.client();
  // the emacs save procedure: open() truncate() write()
  auto handle =
    client.fs->path("/file")->create(O_CREAT | O_RDWR, S_IFREG | 0644);
  handle->write(elle::ConstWeakBuffer("foo\nbar\nbaz\n", 12), 12, 0);
  handle->close();
  handle.reset();
  handle = client.fs->path("/file")->open(O_RDWR, 0);
  BOOST_CHECK(handle);
  client.fs->path("/file")->truncate(0);
  handle->write(elle::ConstWeakBuffer("foo\nbar\n", 8), 8, 0);
  handle->close();
  handle.reset();
  struct stat st;
  client.fs->path("/file")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_size, 8);
  handle = client.fs->path("/file")->open(O_RDWR, 0);
  char buffer[64];
  int count = handle->read(elle::WeakBuffer(buffer, 64), 64, 0);
  BOOST_CHECK_EQUAL(count, 8);
  buffer[count] = 0;
  BOOST_CHECK_EQUAL(buffer, std::string("foo\nbar\n"));
}


ELLE_TEST_SCHEDULED(write_unlink)
{
  auto servers = DHTs(1);
  auto client_1 = servers.client();
  auto client_2 = servers.client();
  auto root_1 = [&]
    {
      ELLE_LOG_SCOPE("fetch client 1 root");
      return client_1.fs->path("/");
    }();
  auto root_2 = [&]
    {
      ELLE_LOG_SCOPE("fetch client 2 root");
      return client_2.fs->path("/");
    }();
  auto handle = [&]
  {
    ELLE_LOG_SCOPE("create file file client 1");
    auto handle =
      root_1->child("file")->create(O_CREAT | O_RDWR, S_IFREG | 0644);
    BOOST_CHECK(handle);
    BOOST_CHECK_EQUAL(handle->write(elle::ConstWeakBuffer("data1"), 5, 0), 5);
    return handle;
  }();
  ELLE_LOG("sync on client 1")
    handle->fsync(true);
  struct stat st;
  ELLE_LOG("check file exists on client 1")
    BOOST_CHECK_NO_THROW(root_1->child("file")->stat(&st));
  ELLE_LOG("check file exists on client 2")
    BOOST_CHECK_NO_THROW(root_2->child("file")->stat(&st));
  ELLE_LOG("read on client 1")
  {
    auto b = elle::Buffer(5);
    BOOST_CHECK_EQUAL(handle->read(b, 5, 0), 5);
    BOOST_CHECK_EQUAL(b, "data1");
  }
  ELLE_LOG("remove file on client 2")
    root_2->child("file")->unlink();
  ELLE_LOG("read on client 1")
  {
    auto b = elle::Buffer(5);
    BOOST_CHECK_EQUAL(handle->read(b, 5, 0), 5);
    BOOST_CHECK_EQUAL(b, "data1");
  }
  ELLE_LOG("write on client 1")
    BOOST_CHECK_EQUAL(handle->write(elle::ConstWeakBuffer("data2"), 5, 0), 5);
  ELLE_LOG("sync on client 1")
    handle->fsync(true);
  ELLE_LOG("read on client 1")
  {
    auto b = elle::Buffer(5);
    BOOST_CHECK_EQUAL(handle->read(b, 5, 0), 5);
    BOOST_CHECK_EQUAL(b, "data2");
  }
  ELLE_LOG("close file on client 1")
    BOOST_CHECK_NO_THROW(handle->close());
  ELLE_LOG("check file does not exist on client 2")
    BOOST_CHECK_THROW(root_1->child("file")->stat(&st), elle::Error);
  ELLE_LOG("check file does not exist on client 2")
    BOOST_CHECK_THROW(root_2->child("file")->stat(&st), elle::Error);
}

ELLE_TEST_SCHEDULED(prefetcher_failure)
{
  auto servers = DHTs(1);
  auto client = servers.client();
  ::Overlay* o = dynamic_cast< ::Overlay*>(client.dht.dht->overlay().get());
  auto root = client.fs->path("/");
  BOOST_CHECK(o);
  auto h = root->child("file")->create(O_CREAT | O_RDWR, S_IFREG | 0644);
  // grow to 2 data blocks
  char buf[16384];
  for (int i=0; i<1024*3; ++i)
    h->write(elle::ConstWeakBuffer(buf, 1024), 1024,  1024*i);
  h->close();
  auto fat = get_fat(root->child("file")->getxattr("user.infinit.fat"));
  BOOST_CHECK_EQUAL(fat.size(), 3);
  o->fail_addresses().insert(fat[1]);
  o->fail_addresses().insert(fat[2]);
  auto handle = root->child("file")->open(O_RDWR, 0);
  BOOST_CHECK_EQUAL(handle->read(elle::WeakBuffer(buf, 16384), 16384, 8192),
                    16384);
  elle::reactor::sleep(200_ms);
  o->fail_addresses().clear();
  BOOST_CHECK_EQUAL(
    handle->read(elle::WeakBuffer(buf, 16384), 16384, 1024 * 1024 + 8192),
    16384);
  BOOST_CHECK_EQUAL(
    handle->read(elle::WeakBuffer(buf, 16384), 16384, 1024 * 1024 * 2 + 8192),
    16384);
}

ELLE_TEST_SCHEDULED(paxos_race)
{
  auto servers = DHTs(1);
  auto c1 = servers.client();
  auto c2 = servers.client();
  auto r1 = c1.fs->path("/");
  auto r2 = c2.fs->path("/");
  ELLE_LOG("create both directories")
  {
    elle::reactor::Thread t1("t1", [&] { r1->child("foo")->mkdir(0700);});
    elle::reactor::Thread t2("t2", [&] { r2->child("bar")->mkdir(0700);});
    elle::reactor::wait({t1, t2});
  }
  ELLE_LOG("check")
  {
    int count = 0;
    c1.fs->path("/")->list_directory(
      [&](std::string const&, struct stat*) { ++count;});
    BOOST_CHECK_EQUAL(count, 4);
    count = 0;
    c2.fs->path("/")->list_directory(
      [&](std::string const&, struct stat*) { ++count;});
    BOOST_CHECK_EQUAL(count, 4);
  }
}

ELLE_TEST_SCHEDULED(data_embed)
{
  auto servers = DHTs(1);
  auto client = servers.client();
  auto root = client.fs->path("/");
  auto h = root->child("file")->create(O_CREAT | O_RDWR, S_IFREG | 0644);
  h->write(elle::ConstWeakBuffer("foo", 3), 3, 0);
  h->close();
  h.reset();
  BOOST_CHECK_EQUAL(
    get_fat(root->child("file")->getxattr("user.infinit.fat")).size(),
    0);

  h = root->child("file")->open(O_RDWR, 0);
  h->write(elle::ConstWeakBuffer("foo", 3), 3, 3);
  h->close();
  h.reset();
  BOOST_CHECK_EQUAL(
    get_fat(root->child("file")->getxattr("user.infinit.fat")).size(),
    0);

  h = root->child("file")->open(O_RDWR, 0);
  char buf[1024] = {0};
  BOOST_CHECK_EQUAL(h->read(elle::WeakBuffer(buf, 64), 64, 0), 6);
  BOOST_CHECK_EQUAL(buf, std::string("foofoo"));
  h->close();
  h.reset();

  h = root->child("file")->open(O_RDWR, 0);
  h->write(elle::ConstWeakBuffer("barbarbaz", 9), 9, 0);
  h->close();
  h.reset();
  BOOST_CHECK_EQUAL(
    get_fat(root->child("file")->getxattr("user.infinit.fat")).size(),
    0);
    h = root->child("file")->open(O_RDWR, 0);
  BOOST_CHECK_EQUAL(h->read(elle::WeakBuffer(buf, 64), 64, 0), 9);
  BOOST_CHECK_EQUAL(buf, std::string("barbarbaz"));
  h->close();
  h.reset();

  h = root->child("file")->open(O_RDWR, 0);
  for (int i = 0; i < 1024; ++i)
    h->write(elle::ConstWeakBuffer(buf, 1024), 1024, 1024*i);
  h->close();
  h.reset();
  BOOST_CHECK_EQUAL(
    get_fat(root->child("file")->getxattr("user.infinit.fat")).size(),
    1);

  h = root->child("file2")->create(O_CREAT | O_RDWR, S_IFREG | 0644);
  h->write(elle::ConstWeakBuffer(buf, 1024), 1024, 0);
  for (int i = 0; i < 1024; ++i)
    h->write(elle::ConstWeakBuffer(buf, 1024), 1024, 1024*i);
  h->write(elle::ConstWeakBuffer(buf, 1024), 1024, 1024*1024);
  h->close();
  h.reset();
  BOOST_CHECK_EQUAL(
    get_fat(root->child("file2")->getxattr("user.infinit.fat")).size(),
    2);
}

ELLE_TEST_SCHEDULED(symlink_perms)
{
  // If we enable paxos, it will cache blocks and feed them back to use.
  // Since we use the Locals dirrectly(no remote), there is no
  // serialization at all when fetching, which means we end up with
  // already decyphered blocks
  auto servers = DHTs(-1);
  auto client1 = servers.client(false);
  auto client2 = servers.client(true);
  ELLE_LOG("create file");
  auto h = client1.fs->path("/foo")->create(O_RDWR |O_CREAT, S_IFREG | 0600);
  ELLE_LOG("write file");
  h->write(elle::ConstWeakBuffer("foo", 3), 3, 0);
  h->close();
  h.reset();
  ELLE_LOG("create symlink");
  client1.fs->path("/foolink")->symlink("/foo");
  BOOST_CHECK_EQUAL(client1.fs->path("/foolink")->readlink(), "/foo");
  ELLE_LOG("client2 check");
  BOOST_CHECK_THROW(client2.fs->path("/foolink")->readlink(), std::exception);
  BOOST_CHECK_THROW(client2.fs->path("/foo")->open(O_RDWR, 0), std::exception);
  auto skey = serialize(client2.dht.dht->keys().K());
  client1.fs->path("/")->setxattr("infinit.auth.setrw", skey, 0);
  client1.fs->path("/")->setxattr("infinit.auth.inherit", "true", 0);
  BOOST_CHECK_THROW(client2.fs->path("/foolink")->readlink(), std::exception);
  BOOST_CHECK_THROW(client2.fs->path("/foo")->open(O_RDWR, 0), std::exception);
  client1.fs->path("/foolink2")->symlink("/foo");
  BOOST_CHECK_NO_THROW(client2.fs->path("/foolink2")->readlink());
  client1.fs->path("/foolink")->setxattr("infinit.auth.setr", skey, 0);
  BOOST_CHECK_NO_THROW(client2.fs->path("/foolink")->readlink());
}

ELLE_TEST_SCHEDULED(short_hash_key)
{
  auto servers = DHTs(1);
  auto client1 = servers.client();
  auto key = elle::cryptography::rsa::keypair::generate(512);
  auto serkey = elle::serialization::json::serialize(key.K());
  client1.fs->path("/")->setxattr("infinit.auth.setr", serkey.string(), 0);
  auto jsperms = client1.fs->path("/")->getxattr("infinit.auth");
  std::stringstream s(jsperms);
  auto jperms = elle::json::read(s);
  auto a = boost::any_cast<elle::json::Array>(jperms);
  BOOST_CHECK_EQUAL(a.size(), 2);
  auto hash = boost::any_cast<std::string>(
    boost::any_cast<elle::json::Object>(a.at(1)).at("name"));
  ELLE_TRACE("got hash: %s", hash);
  client1.fs->path("/")->setxattr("infinit.auth.clear", hash, 0);
  jsperms = client1.fs->path("/")->getxattr("infinit.auth");
  s.str(jsperms);
  jperms = elle::json::read(s);
  a = boost::any_cast<elle::json::Array>(jperms);
  BOOST_CHECK_EQUAL(a.size(), 1);
  BOOST_CHECK_THROW(client1.fs->path("/")->setxattr("infinit.auth.clear", "#gogol", 0),
                    std::exception);
}

ELLE_TEST_SCHEDULED(rename_exceptions)
{
  // Ensure source does not get erased if rename fails under various conditions
  auto servers = DHTs(-1);
  auto client1 = servers.client();
  client1.fs->path("/");
  auto client2 = servers.client(true);
  BOOST_CHECK_THROW(client2.fs->path("/foo")->mkdir(0666), std::exception);
  auto c2key = elle::serialization::json::serialize(client2.dht.dht->keys().K()).string();
  client1.fs->path("/")->setxattr("infinit.auth.setrw", c2key, 0);
  ELLE_TRACE("create target inaccessible dir");
  client1.fs->path("/dir")->mkdir(0600);
  ELLE_TRACE("mkdir without perms");
  BOOST_CHECK_THROW(client2.fs->path("/dir/foo")->mkdir(0666), std::exception);
  ELLE_TRACE("create source dir");
  client2.fs->path("/foo")->mkdir(0666);
  ELLE_TRACE("Rename");
  try
  {
    client2.fs->path("/foo")->rename("/dir/foo");
    BOOST_CHECK(false);
  }
  catch (elle::Error const&e)
  {
    ELLE_TRACE("exc %s", e);
  }
  struct stat st;
  client2.fs->path("/foo")->stat(&st);
  BOOST_CHECK(S_ISDIR(st.st_mode));
  // check again with read-only access
  client1.fs->path("/dir")->setxattr("infinit.auth.setr", c2key, 0);
  ELLE_TRACE("Rename2");
  try
  {
    client2.fs->path("/foo")->rename("/dir/foo");
    BOOST_CHECK(false);
  }
  catch (elle::Error const&e)
  {
    ELLE_TRACE("exc %s", e);
  }
  client2.fs->path("/foo")->stat(&st);
  BOOST_CHECK(S_ISDIR(st.st_mode));
}


ELLE_TEST_SCHEDULED(erased_group)
{
  auto servers = DHTs(-1);
  auto client1 = servers.client();
  auto client2 = servers.client(true);
  auto c2key = elle::serialization::json::serialize(client2.dht.dht->keys().K()).string();
  client1.fs->path("/");
  client1.fs->path("/")->setxattr("infinit.group.create", "grp", 0);
  client1.fs->path("/")->setxattr("infinit.group.add", "grp:" + c2key, 0);
  client1.fs->path("/")->setxattr("infinit.auth.setrw", "@grp", 0);
  client1.fs->path("/")->setxattr("infinit.auth.inherit", "true", 0);
  client2.fs->path("/dir")->mkdir(0666);
  client2.fs->path("/file")->create(O_RDWR | O_CREAT, 0666)->write(
    elle::ConstWeakBuffer("foo", 3), 3, 0);
  client1.fs->path("/")->setxattr("infinit.group.delete", "grp", 0);
  // cant write to /, because last author is a group member: it fails validation
  BOOST_CHECK_THROW(client1.fs->path("/dir2")->mkdir(0666), elle::reactor::filesystem::Error);
  // we have inherit enabled, copy_permissions will fail on the missing group
  BOOST_CHECK_THROW(client2.fs->path("/dir/dir")->mkdir(0666), elle::reactor::filesystem::Error);
  client2.fs->path("/file")->open(O_RDWR, 0644)->write(
    elle::ConstWeakBuffer("bar", 3), 3, 0);
}

ELLE_TEST_SCHEDULED(erased_group_recovery)
{
  auto servers = DHTs(-1);
  auto client1 = servers.client();
  auto client2 = servers.client(true);
  client1.fs->path("/");
  auto c2key = elle::serialization::json::serialize(client2.dht.dht->keys().K()).string();
  client1.fs->path("/")->setxattr("infinit.group.create", "grp", 0);
  client1.fs->path("/")->setxattr("infinit.group.add", "grp:" + c2key, 0);
  ELLE_TRACE("set group ACL");
  client1.fs->path("/")->setxattr("infinit.auth.setrw", "@grp", 0);
  client1.fs->path("/")->setxattr("infinit.auth.inherit", "true", 0);
  client1.fs->path("/dir")->mkdir(0666);
  ELLE_TRACE("delete group");
  client1.fs->path("/")->setxattr("infinit.group.delete", "grp", 0);
  ELLE_TRACE("list auth");
  auto jsperms = client1.fs->path("/dir")->getxattr("infinit.auth");
  std::stringstream s(jsperms);
  auto jperms = elle::json::read(s);
  auto a = boost::any_cast<elle::json::Array>(jperms);
  BOOST_CHECK_EQUAL(a.size(), 2);
  auto hash = boost::any_cast<std::string>(
    boost::any_cast<elle::json::Object>(a.at(1)).at("name"));
  ELLE_TRACE("got hash: %s", hash);
  ELLE_TRACE("clear group from auth");
  client1.fs->path("/dir")->setxattr("infinit.auth.clear", hash, 0);
  ELLE_TRACE("recheck auth");
  jsperms = client1.fs->path("/dir")->getxattr("infinit.auth");
  s.str(jsperms);
  jperms = elle::json::read(s);
  a = boost::any_cast<elle::json::Array>(jperms);
  BOOST_CHECK_EQUAL(a.size(), 1);
  client1.fs->path("/")->setxattr("infinit.auth.clear", hash, 0);
  jsperms = client1.fs->path("/")->getxattr("infinit.auth");
  s.str(jsperms);
  jperms = elle::json::read(s);
  a = boost::any_cast<elle::json::Array>(jperms);
  BOOST_CHECK_EQUAL(a.size(), 1);
}

ELLE_TEST_SCHEDULED(remove_permissions)
{
  auto servers = DHTs(-1);
  auto client1 = servers.client(false);
  auto client2 = servers.client(true);
  auto skey = serialize(client2.dht.dht->keys().K());
  client1.fs->path("/dir")->mkdir(0666);
  client1.fs->path("/")->setxattr("infinit.auth.setr", skey, 0);
  client1.fs->path("/dir")->setxattr("infinit.auth.setrw", skey, 0);
  client1.fs->path("/dir")->setxattr("infinit.auth.inherit", "true", 0);
  auto h = client2.fs->path("/dir/file")->create(O_CREAT|O_TRUNC|O_RDWR, 0666);
  h->write(elle::ConstWeakBuffer("foo", 3), 3, 0);
  h->close();
  h.reset();
  h = client1.fs->path("/dir/file")->open(O_RDONLY, 0666);
  char buf[512] = {0};
  int len = h->read(elle::WeakBuffer(buf, 512), 512, 0);
  BOOST_CHECK_EQUAL(len, 3);
  BOOST_CHECK_EQUAL(buf, std::string("foo"));
  h->close();
  h.reset();
  client2.fs->path("/dir/file")->unlink();
  struct stat st;
  client2.fs->path("/dir")->stat(&st);
  BOOST_CHECK(st.st_mode & S_IFDIR);
  int count = 0;
  client1.fs->path("/dir")->list_directory(
      [&](std::string const&, struct stat*) { ++count;});
  BOOST_CHECK_EQUAL(count, 2);

  h = client1.fs->path("/file")->create(O_CREAT|O_TRUNC|O_RDWR, 0666);
  h->write(elle::ConstWeakBuffer("bar", 3), 3, 0);
  h->close();
  h.reset();
  client1.fs->path("/file")->setxattr("infinit.auth.setr", skey, 0);
  BOOST_CHECK_THROW(client2.fs->path("/file")->unlink(), std::exception);
  client1.fs->path("/file")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_THROW(client2.fs->path("/file")->unlink(), std::exception);
  client1.fs->path("/file")->setxattr("infinit.auth.setr", skey, 0);
  client1.fs->path("/")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_THROW(client2.fs->path("/file")->unlink(), std::exception);
  h = client1.fs->path("/file")->open(O_RDONLY, 0666);
  len = h->read(elle::WeakBuffer(buf, 512), 512, 0);
  BOOST_CHECK_EQUAL(len, 3);
  BOOST_CHECK_EQUAL(buf, std::string("bar"));

  client1.fs->path("/dir2")->mkdir(0666);
  BOOST_CHECK_THROW(client2.fs->path("/dir2")->rmdir(), std::exception);
  client1.fs->path("/")->setxattr("infinit.auth.setr", skey, 0);
  client1.fs->path("/dir2")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_THROW(client2.fs->path("/dir2")->rmdir(), std::exception);
  client1.fs->path("/")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_NO_THROW(client2.fs->path("/dir2")->rmdir());
}

ELLE_TEST_SCHEDULED(create_excl)
{
  auto servers = DHTs(1, {}, with_cache = true);
  auto client1 = servers.client(false);
  auto client2 = servers.client(false);
  // cache feed
  client1.fs->path("/");
  client2.fs->path("/");
  client1.fs->path("/file")->create(O_RDWR|O_CREAT|O_EXCL, 0644);
  BOOST_CHECK_THROW(
    client2.fs->path("/file")->create(O_RDWR|O_CREAT|O_EXCL, 0644),
    elle::reactor::filesystem::Error);
  // again, now that our cache knows the file
  BOOST_CHECK_THROW(
    client2.fs->path("/file")->create(O_RDWR|O_CREAT|O_EXCL, 0644),
    elle::reactor::filesystem::Error);
}

#if 0
ELLE_TEST_SCHEDULED(multiple_writers)
{
  memo::silo::Memory::Blocks blocks;
  struct stat st;
  auto servers = DHTs(1, {},
               with_cache = true,
               storage = std::make_unique<memo::silo::Memory>(blocks));
  auto client = servers.client(false);
  char buffer[1024];
  initialize(buffer);
  auto b = elle::WeakBuffer(buffer, 1024);
  char buffer2[1024];
  auto b2 = elle::WeakBuffer(buffer2, 1024);
  {
    auto h1 = client.fs->path("/file")->create(O_RDWR|O_CREAT|O_EXCL, 0644);
    auto h2 = client.fs->path("/file")->open(O_RDWR, 0644);
    h1->write(b, 1024, 0);
    auto sz = h2->read(b2, 1024, 0);
    BOOST_CHECK_EQUAL(sz, 1024);
    BOOST_CHECK(!memcmp(buffer, buffer2, 1024));
    h2->write(b, 1024, 512);
    sz = h1->read(b2, 1024, 512);
    BOOST_CHECK_EQUAL(sz, 1024);
    BOOST_CHECK(!memcmp(buffer, buffer2, 1024));
  }
  bool do_yield = true;
  auto seq_write = [&] {
      for (int i=0; i<5; ++i)
      {
        auto h = client.fs->path("/file")->open(O_RDWR, 0644);
        for (int o = 0; o < 1024*30; ++o)
        {
          h->write(b, 1024, o*1024);
          if (do_yield)
            elle::reactor::yield();
        }
      }
  };
  {
    do_yield = true;
    elle::reactor::Thread t1("writer 1", seq_write);
    elle::reactor::Thread t2("writer 2", seq_write);
    elle::reactor::Thread t3("writer 3", seq_write);
    elle::reactor::Thread t4("writer 4", seq_write);
    elle::reactor::wait({t1, t2, t3, t4});
    client.fs->path("/file")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_size, 1024 * 1024 * 30);
    auto h = client.fs->path("/file")->open(O_RDONLY, 0644);
    for (int o = 0; o < 1024*30; ++o)
    {
      h->read(b2, 1024, o * 1024);
      BOOST_CHECK(!memcmp(buffer, buffer2, 1024));
    }
    BOOST_CHECK_LE(blocks.size(), 50);
  }
  {
    do_yield = true;
    elle::reactor::Thread t1("writer 1", seq_write);
    for (int i=0; i<1024 * 10; ++i)
      elle::reactor::yield();
    elle::reactor::Thread t2("writer 2", seq_write);
    for (int i=0; i<1024 * 10; ++i)
      elle::reactor::yield();
    elle::reactor::Thread t3("writer 3", seq_write);
    for (int i=0; i<1024 * 10; ++i)
      elle::reactor::yield();
    elle::reactor::Thread t4("writer 4", seq_write);
    elle::reactor::wait({t1, t2, t3, t4});
    client.fs->path("/file")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_size, 1024 * 1024 * 30);
    auto h = client.fs->path("/file")->open(O_RDONLY, 0644);
    for (int o = 0; o < 1024*30; ++o)
    {
      h->read(b2, 1024, o * 1024);
      BOOST_CHECK(!memcmp(buffer, buffer2, 1024));
    }
    BOOST_CHECK_LE(blocks.size(), 50);
  }
  {
    do_yield = false;
    elle::reactor::Thread t1("writer 1", seq_write);
    elle::reactor::Thread t2("writer 2", seq_write);
    elle::reactor::Thread t3("writer 3", seq_write);
    elle::reactor::Thread t4("writer 4", seq_write);
    elle::reactor::wait({t1, t2, t3, t4});
    client.fs->path("/file")->stat(&st);
    BOOST_CHECK_EQUAL(st.st_size, 1024 * 1024 * 30);
    auto h = client.fs->path("/file")->open(O_RDONLY, 0644);
    for (int o = 0; o < 1024*30; ++o)
    {
      h->read(b2, 1024, o * 1024);
      BOOST_CHECK(!memcmp(buffer, buffer2, 1024));
    }
    BOOST_CHECK_LE(blocks.size(), 50);
  }
  client.fs->path("/file2")->create(O_RDWR|O_CREAT, 0644);
  auto random_write = [&] {
    for (int i=0; i<100; ++i)
    {
      auto h = client.fs->path("/file2")->open(O_RDWR, 0644);
      auto o = (rand()%30)*1024 * 1024 +  (rand()%1024) * 1024 + (rand()%1024);
      h->write(b, 1024, o);
      if (do_yield)
        elle::reactor::yield();
    }
  };
  {
    do_yield = true;
    elle::reactor::Thread t1("writer 1", random_write);
    elle::reactor::Thread t2("writer 2", random_write);
    elle::reactor::Thread t3("writer 3", random_write);
    elle::reactor::Thread t4("writer 4", random_write);
    elle::reactor::wait({t1, t2, t3, t4});
    client.fs->path("/file2")->stat(&st);
    ELLE_TRACE("resulting file: %s bytes", st.st_size);
    auto h = client.fs->path("/file")->open(O_RDONLY, 0644);
    for (int o=0; o < st.st_size; o+= 1024)
    {
      auto len = std::min(off_t(1024), off_t(st.st_size-o));
      h->read(elle::WeakBuffer(buffer, len), len, o);
    }
  }
}
#endif

ELLE_TEST_SCHEDULED(sparse_file)
{
  // Under windows, a 'cp' causes a ftruncate(target_size), so check that it
  // works
  auto servers = DHTs(-1);
  auto client = servers.client();
  client.fs->path("/");
  auto const size = 2'500'000;
  for (int iter = 0; iter < 2; ++iter)
  { // run twice to get 'non-existing' and 'existing' initial states
    auto h = client.fs->path("/file")->create(O_RDWR | O_CREAT|O_TRUNC, 0666);
    char buf[191];
    char obuf[191];
    for (auto i: boost::irange(0, 191))
      buf[i] = i%191;
    int sz = 191 * (1 + size/191);
    h->ftruncate(sz);
    for (auto i: boost::irange(0, size, 191))
      h->write(elle::ConstWeakBuffer(buf, 191), 191, i);
    h->close();
    h = client.fs->path("/file")->open(O_RDONLY, 0666);
    for (auto i: boost::irange(0, size, 191))
    {
      h->read(elle::WeakBuffer(obuf, 191), 191, i);
      // Don't send 2.5M lines of logs: call BOOST_CHECK at most once.
      if (memcmp(obuf, buf, 191))
        BOOST_CHECK(!memcmp(obuf, buf, 191));
    }
    // Show the result at least once.
    BOOST_CHECK(!memcmp(obuf, buf, 191));
  }
}

ELLE_TEST_SCHEDULED(create_race)
{
  auto dhts = DHTs(3 /*, {},version = elle::Version(0, 6, 0)*/);
  auto client1 = dhts.client(false, {}, /*version = elle::Version(0,6,0),*/ yielding_overlay = true);
  auto client2 = dhts.client(false, {}, /*version = elle::Version(0,6,0),*/ yielding_overlay = true);
  client1.fs->path("/");
  elle::reactor::Thread tpoll("poller", [&] {
      while (true)
      {
        try
        {
          auto h = client2.fs->path("/file")->open(O_RDONLY, 0644);
          char buf[1024];
          int len = h->read(elle::WeakBuffer(buf, 1024), 1024, 0);
          auto content = std::string(buf, buf+len);
          if (content.empty())
            ELLE_LOG("Empty content");
          else
          {
            BOOST_CHECK_EQUAL(content, "foo");
            break;
          }
        }
        catch(rfs::Error const& e)
        {
          BOOST_CHECK_EQUAL(e.error_code(), ENOENT);
        }
        elle::reactor::yield();
      }
  });
  elle::reactor::yield();
  elle::reactor::yield();
  try
  {
    auto h = client1.fs->path("/file")->create(O_CREAT|O_RDWR, 0644);
    h->write(elle::ConstWeakBuffer("foo", 3), 3, 0);
    h->close();
  }
  catch(elle::Error const& e)
  {
    ELLE_WARN("kraboum %s", e);
  }
  elle::reactor::wait(tpoll);
}

int write_file(std::shared_ptr<elle::reactor::filesystem::Path> p,
               std::string const& content,
               int mode = O_CREAT|O_TRUNC|O_RDWR,
               int offset = 0)
{
  auto h = p->create(mode, 0666);
  auto sz = h->write(elle::ConstWeakBuffer(content.data(), content.size()), content.size(), offset);
  h->close();
  return sz;
}

std::string read_file(std::shared_ptr<elle::reactor::filesystem::Path> p,
                      int size = 4096,
                      int offset = 0)
{
  auto h = p->open(O_RDONLY, 0666);
  std::string buf(size, '\0');
  auto sz = h->read(elle::WeakBuffer(elle::unconst(buf.data()), size), size, offset);
  buf.resize(sz);
  h->close();
  return buf;
}

int directory_count(std::shared_ptr<elle::reactor::filesystem::Path> p)
{
  int count = 0;
  p->list_directory([&count](std::string const&, struct stat*) {++count;});
  if (count == 0)
    throw rfs::Error(42, "directory listing error");
  return count;
}

size_t file_size(std::shared_ptr<elle::reactor::filesystem::Path> p)
{
  struct stat st;
  p->stat(&st);
  return st.st_size;
}

void read_all(std::shared_ptr<elle::reactor::filesystem::Path> p)
{
  auto h = p->open(O_RDONLY, 0644);
  char buf[16384];
  int i = 0;
  while (h->read(elle::WeakBuffer(buf, 16384), 16384, i*16384) == 16384)
    ++i;
}

ELLE_TEST_SCHEDULED(acls)
{
  auto servers = DHTs(3, {}, dht::consensus_builder = no_cheat_consensus());
  auto client0 = servers.client(false, {}, user_name="user0");
  auto& fs0 = client0.fs;
  auto client1 = servers.client(true, {}, user_name="user1");
  auto& fs1 = client1.fs;
  auto skey = serialize(client1.dht.dht->keys().K());

  write_file(fs0->path("/test"), "Test");
  BOOST_CHECK_EQUAL(directory_count(fs0->path("/")), 3);
  BOOST_CHECK_THROW(directory_count(fs1->path("/")), rfs::Error);
  BOOST_CHECK_THROW(write_file(fs1->path("/test2"), "bar"), rfs::Error);
  fs0->path("/")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/")), 3);
  BOOST_CHECK_THROW(read_file(fs1->path("/test")), rfs::Error);
  BOOST_CHECK_THROW(fs1->path("/test")->unlink(), rfs::Error);
  fs0->path("/test")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/test")), "Test");
  fs0->path("/test")->setxattr("infinit.auth.clear", skey, 0);
  BOOST_CHECK_THROW(read_file(fs1->path("/test")), rfs::Error);
  fs0->path("/test")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/test")), "Test");

  fs0->path("/dir1")->mkdir(0600);
  write_file(fs0->path("/dir1/pan"), "foo");
  BOOST_CHECK_THROW(fs1->path("/dir1")->list_directory([](std::string const&, struct stat*) {}), rfs::Error);
  BOOST_CHECK_THROW(read_file(fs1->path("/dir1/pan")), rfs::Error);
  BOOST_CHECK_THROW(write_file(fs1->path("/dir1/coin"), "foo"), rfs::Error);
  // test by user name
  write_file(fs0->path("/byuser"), "foo");
  BOOST_CHECK_THROW(read_file(fs1->path("/byuser")), rfs::Error);
  fs0->path("/byuser")->setxattr("infinit.auth.setrw", "user1", 0);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/byuser")), "foo");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/test")), "Test");
  // inheritance
  fs0->path("/dirs")->mkdir(0600);
  fs0->path("/dirs")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dirs")), 2);
  fs0->path("/dirs")->setxattr("infinit.auth.inherit", "true", 0);
  write_file(fs0->path("/dirs/coin"), "foo");
  fs0->path("/dirs/dir")->mkdir(0600);
  write_file(fs0->path("/dirs/dir/coin"), "foo");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/dirs/coin")), "foo");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/dirs/dir/coin")), "foo");
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dirs")), 4);
  // readonly
  fs0->path("/dir2")->mkdir(0600);
  fs0->path("/dir2")->setxattr("infinit.auth.setr", skey, 0);
  write_file(fs0->path("/dir2/coin"), "foo");
  fs0->path("/dir2/coin")->setxattr("infinit.auth.setr", skey, 0);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/dir2/coin")), "foo");
  BOOST_CHECK_THROW(write_file(fs1->path("/dir2/coin"), "coin"), rfs::Error);
  BOOST_CHECK_THROW(write_file(fs1->path("/dir2/pan"), "coin"), rfs::Error);
  // world-readable
  fs0->path("/")->setxattr("infinit.auth.inherit", "false", 0);
  fs0->path("/dir3")->mkdir(0600);
  fs0->path("/dir3/dir")->mkdir(0600);
  write_file(fs0->path("/dir3/file"), "foo");
  BOOST_CHECK_EQUAL(directory_count(fs0->path("/dir3")), 4);
  BOOST_CHECK_THROW(directory_count(fs1->path("/dir3")), rfs::Error);
  // open dir3
  fs0->path("/dir3")->chmod(0644);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dir3")), 4);
  BOOST_CHECK_THROW(fs1->path("/dir3/tdir")->mkdir(0644), rfs::Error);
  BOOST_CHECK_THROW(directory_count(fs1->path("/dir3/dir")), rfs::Error);
  // close dir3
  fs0->path("/dir3")->chmod(0600);
  BOOST_CHECK_THROW(directory_count(fs1->path("/dir3")), rfs::Error);
  fs0->path("/dir3")->chmod(0644);
  fs0->path("/dir3/file")->chmod(0644);
  fs0->path("/dir3/dir")->chmod(0644);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dir3/dir")), 2);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/dir3/file")), "foo");
  BOOST_CHECK_THROW(write_file(fs1->path("/dir3/file"), "babar"), rfs::Error);
  write_file(fs0->path("/dir3/file"), "bim");
  BOOST_CHECK_EQUAL(read_file(fs0->path("/dir3/file")), "bim");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/dir3/file")), "bim");
  fs0->path("/dir3/dir2")->mkdir(0644);
  BOOST_CHECK_EQUAL(directory_count(fs0->path("/dir3")), 5);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dir3")), 5);
  fs0->path("/dir3/file")->chmod(0600);
  write_file(fs0->path("/dir3/file"), "foo2");
  BOOST_CHECK_EQUAL(read_file(fs0->path("/dir3/file")), "foo2");
  // world-writable
  fs0->path("/dir4")->mkdir(0600);
  BOOST_CHECK_THROW(directory_count(fs1->path("/dir4")), rfs::Error);
  fs0->path("/dir4")->chmod(0666);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dir4")), 2);
  write_file(fs1->path("/dir4/file"), "foo");
  fs1->path("/dir4/dir")->mkdir(0600);
  BOOST_CHECK_THROW(read_file(fs0->path("/dir4/file")), rfs::Error);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/dir4/file")), "foo");
  BOOST_CHECK_THROW(directory_count(fs0->path("/dir4/dir")), rfs::Error);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dir4/dir")), 2);
  fs0->path("/dir4")->chmod(0600);
  BOOST_CHECK_THROW(read_file(fs1->path("/dir4/file")), rfs::Error);
  write_file(fs0->path("/file5"), "foo");
  fs0->path("/file5")->chmod(0666);
  write_file(fs1->path("/file5"), "bar");
  BOOST_CHECK_EQUAL(read_file(fs0->path("/file5")), "bar");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/file5")), "bar");
  fs0->path("/file5")->chmod(0644);
  BOOST_CHECK_THROW(write_file(fs1->path("/file5"), "babar"), rfs::Error);
  BOOST_CHECK_EQUAL(read_file(fs0->path("/file5")), "bar");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/file5")), "bar");
  //groups
  write_file(fs0->path("/g1"), "foo");
  BOOST_CHECK_EQUAL(read_file(fs0->path("/g1")), "foo");
  BOOST_CHECK_THROW(read_file(fs1->path("/g1")), rfs::Error);
  fs0->path("/")->setxattr("infinit.group.create", "group1", 0);
  fs0->path("/")->setxattr("infinit.group.add", "group1:user1", 0);
  fs0->path("/g1")->setxattr("infinit.auth.setrw", "@group1", 0);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/g1")), "foo");
  fs0->path("/")->setxattr("infinit.group.remove", "group1:user1", 0);
  write_file(fs0->path("/g2"), "foo");
  fs0->path("/g2")->setxattr("infinit.auth.setrw", "@group1", 0);
  BOOST_CHECK_THROW(read_file(fs1->path("/g2")), rfs::Error);
  fs0->path("/")->setxattr("infinit.group.add", "group1:user1", 0);
  BOOST_CHECK_EQUAL(read_file(fs1->path("/g2")), "foo");
  write_file(fs1->path("/g2"), "bar");
  BOOST_CHECK_EQUAL(read_file(fs0->path("/g2")), "bar");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/g2")), "bar");
  fs0->path("/")->setxattr("infinit.group.remove", "group1:user1", 0);
  BOOST_CHECK_EQUAL(read_file(fs0->path("/g2")), "bar");
  fs0->path("/")->setxattr("infinit.group.add", "group1:user1", 0);
  BOOST_CHECK_EQUAL(read_file(fs0->path("/g2")), "bar");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/g2")), "bar");
  // group admin
  fs0->path("/")->setxattr("infinit.group.addadmin", "group1:user1", 0);
  fs1->path("/")->setxattr("infinit.group.add", "group1:user0", 0);
  write_file(fs1->path("/g3"), "bar");
  BOOST_CHECK_THROW(read_file(fs0->path("/g3")), rfs::Error);
  fs1->path("/g3")->setxattr("infinit.auth.setrw", "@group1", 0);
  BOOST_CHECK_EQUAL(read_file(fs0->path("/g3")), "bar");
  fs0->path("/")->setxattr("infinit.group.removeadmin", "group1:user1", 0);
  BOOST_CHECK_THROW(fs1->path("/")->setxattr("infinit.group.remove", "group1:user0", 0), rfs::Error);
  BOOST_CHECK_THROW(fs1->path("/")->setxattr("infinit.group.removeadmin", "group1:user0", 0), rfs::Error);
  //incorrect stuff, check it doesn't crash us
  BOOST_CHECK_THROW(fs1->path("/")->setxattr("infinit.group.add", "group1:user0", 0), rfs::Error);
  BOOST_CHECK_THROW(fs0->path("/")->setxattr("infinit.group.create", "group1", 0), rfs::Error);
  BOOST_CHECK_EQUAL(read_file(fs0->path("/g1")), "foo");
  fs0->path("/")->setxattr("infinit.group.remove", "group1:user1", 0);
  fs0->path("/")->setxattr("infinit.group.remove", "group1:user1", 0);
  BOOST_CHECK_THROW(fs0->path("/")->setxattr("infinit.group.add", "group1:group1", 0), rfs::Error);
  BOOST_CHECK_THROW(fs0->path("/")->setxattr("infinit.group.add", "nosuch:user1", 0), rfs::Error);
  BOOST_CHECK_THROW(fs0->path("/")->setxattr("infinit.group.add", "group1:nosuch", 0), rfs::Error);
  BOOST_CHECK_EQUAL(read_file(fs0->path("/g1")), "foo");
  BOOST_CHECK_THROW(fs0->path("/")->setxattr("infinit.group.remove", "group1:nosuch", 0), rfs::Error);
  fs0->path("/")->setxattr("infinit.group.delete", "group1", 0);

  // removal
  //test the xattrs we'll use
  fs0->path("/dirrm")->mkdir(0600);
  fs0->path("/dirrm")->setxattr("infinit.auth.setrw", "user1", 0);
  write_file(fs0->path("/dirrm/rm"), "pan");
  BOOST_CHECK_EQUAL(directory_count(fs0->path("/dirrm")), 3);
  BOOST_CHECK_EQUAL(directory_count(fs1->path("/dirrm")), 3);
  BOOST_CHECK_EQUAL(read_file(fs0->path("/dirrm/rm")), "pan");
  std::string block = fs0->path("/dirrm/rm")->getxattr("infinit.block.address");
  block = block.substr(3, block.size()-5);
  fs0->path("/")->setxattr("infinit.fsck.rmblock", block, 0);
  BOOST_CHECK_THROW(read_file(fs0->path("/dirrm/rm")), rfs::Error);
  BOOST_CHECK_EQUAL(directory_count(fs0->path("/dirrm")), 3);
  fs0->path("/dirrm")->setxattr("user.infinit.fsck.unlink", "rm", 0);
  BOOST_CHECK_EQUAL(directory_count(fs0->path("/dirrm")), 2);

  write_file(fs0->path("/dirrm/rm2"), "foo");
  BOOST_CHECK_EQUAL(directory_count(fs0->path("/dirrm")), 3);
  block = fs0->path("/dirrm/rm2")->getxattr("infinit.block.address");
  block = block.substr(3, block.size()-5);
  ELLE_LOG("hop");
  BOOST_CHECK_THROW(fs1->path("/")->setxattr("infinit.fsck.rmblock", block, 0), rfs::Error);
  ELLE_LOG("hop1");
  BOOST_CHECK_EQUAL(read_file(fs0->path("/dirrm/rm2")), "foo");
  ELLE_LOG("hop2");
  fs0->path("/dirrm/rm2")->unlink();
  ELLE_LOG("hop3");
  // removal chb
  auto f = fs0->path("/dirrm/rm3")->create(O_CREAT|O_RDWR, 0644);
  char buffer[16384];
  for (int i=0; i<100; ++i)
    f->write(elle::ConstWeakBuffer(buffer, 16384), 16384, 16384*i);
  f->close();
  f.reset();
  auto sfat = fs0->path("/dirrm/rm3")->getxattr("infinit.fat");
  auto fat = get_fat(sfat);
  BOOST_CHECK_THROW(fs1->path("/dirrm")->setxattr("infinit.fsck.rmblock",
    elle::sprintf("%x", fat[0]), 0), rfs::Error);
  read_all(fs0->path("/dirrm/rm3"));
  fs0->path("/dirrm")->setxattr("infinit.fsck.rmblock",
    elle::sprintf("%x", fat[0]), 0);
  BOOST_CHECK_THROW(read_all(fs0->path("/dirrm/rm3")), rfs::Error);
}

ELLE_TEST_SCHEDULED(write_read)
{
  auto servers = DHTs(3);
  auto client = servers.client();
  auto& fs = client.fs;

  write_file(fs->path("/test"), "Test");
  BOOST_CHECK_EQUAL(read_file(fs->path("/test"), 4096), "Test");
  write_file(fs->path("/test"), "coin", O_WRONLY, 4);
  BOOST_CHECK_EQUAL(read_file(fs->path("/test"), 4096), "Testcoin");
  BOOST_CHECK_EQUAL(file_size(fs->path("/test")), 8);
  fs->path("/test")->unlink();
}

ELLE_TEST_SCHEDULED(basic)
{
  auto servers = DHTs(3);
  auto client = servers.client();
  auto& fs = client.fs;

  BOOST_CHECK_THROW(read_file(fs->path("/foo")), rfs::Error);

  ELLE_LOG("truncate")
  {
    auto h = fs->path("/tt")->create(O_RDWR|O_TRUNC|O_CREAT, 0666);
    char buffer[16384];
    initialize(buffer);
    for (int i=0; i<100; ++i)
      h->write(elle::ConstWeakBuffer(buffer, 16384), 16384, 16384*i);
    h->close();
    h = fs->path("/tt")->open(O_RDWR, 0666);
    h->ftruncate(0);
    h->write(elle::ConstWeakBuffer(buffer, 16384), 16384, 0);
    h->write(elle::ConstWeakBuffer(buffer, 12288), 12288, 16384);
    h->write(elle::ConstWeakBuffer(buffer, 3742), 3742, 16384+12288);
    h->ftruncate(32414);
    h->ftruncate(32413);
    h->close();
    BOOST_CHECK_EQUAL(file_size(fs->path("/tt")), 32413);
    fs->path("/tt")->unlink();
  }

  ELLE_LOG("hard-link")
  {
    write_file(fs->path("/test"), "Test");
    fs->path("/test")->link("/test2");
    write_file(fs->path("/test2"), "coinB", O_WRONLY, 4);
    BOOST_CHECK_EQUAL(file_size(fs->path("/test")), 9);
    BOOST_CHECK_EQUAL(file_size(fs->path("/test2")), 9);
    BOOST_CHECK_EQUAL(read_file(fs->path("/test")), "TestcoinB");
    BOOST_CHECK_EQUAL(read_file(fs->path("/test2")), "TestcoinB");
    write_file(fs->path("/test"), "coinA", O_WRONLY, 9);
    BOOST_CHECK_EQUAL(file_size(fs->path("/test")), 14);
    BOOST_CHECK_EQUAL(file_size(fs->path("/test2")), 14);
    BOOST_CHECK_EQUAL(read_file(fs->path("/test")), "TestcoinBcoinA");
    BOOST_CHECK_EQUAL(read_file(fs->path("/test2")), "TestcoinBcoinA");
    fs->path("/test")->unlink();
    BOOST_CHECK_EQUAL(read_file(fs->path("/test2")), "TestcoinBcoinA");
    fs->path("/test2")->unlink();

    // hardlink opened handle
    write_file(fs->path("/test"), "Test");
    auto h = fs->path("/test")->open(O_RDWR, 0666);
    h->write(elle::ConstWeakBuffer("a", 1), 1, 4);
    fs->path("/test")->link("/test2");
    h->write(elle::ConstWeakBuffer("b", 1), 1, 5);
    h->close();
    BOOST_CHECK_EQUAL(read_file(fs->path("/test")), "Testab");
    BOOST_CHECK_EQUAL(read_file(fs->path("/test2")), "Testab");
    fs->path("/test")->unlink();
    fs->path("/test2")->unlink();
  }

  ELLE_LOG("Holes")
  {
    auto h = fs->path("/test")->create(O_RDWR|O_CREAT, 0644);
    h->write(elle::ConstWeakBuffer("foo", 3), 3, 0);
    h->write(elle::ConstWeakBuffer("foo", 3), 3, 13);
    h->close();
    auto content = read_file(fs->path("/test"));
    BOOST_CHECK_EQUAL(file_size(fs->path("/test")), 16);
    char expect[] = {'f','o','o',0,0,0,0,0,0,0,0,0,0,'f','o','o'};
    BOOST_CHECK_EQUAL(std::string(expect, expect+16), content);
    fs->path("/test")->unlink();
  }

  ELLE_LOG("use after unlink")
  {
    auto h = fs->path("/test")->create(O_RDWR|O_CREAT, 0644);
    h->write(elle::ConstWeakBuffer("foo", 3), 3, 0);
    fs->path("/test")->unlink();
    h->write(elle::ConstWeakBuffer("foo", 3), 3, 3);
    char buf[7] = {0};
    auto count = h->read(elle::WeakBuffer(buf, 6), 6, 0);
    BOOST_CHECK_EQUAL(count, 6);
    BOOST_CHECK_EQUAL(buf, "foofoo");
    h->close();
    BOOST_CHECK_EQUAL(directory_count(fs->path("/")), 2);
  }

  ELLE_LOG("rename")
  {
    write_file(fs->path("/test"), "Test");
    fs->path("/test")->rename("/test2");
    write_file(fs->path("/test3"), "foo");
    fs->path("/test2")->rename("/test3");
    BOOST_CHECK_EQUAL(read_file(fs->path("/test3")), "Test");
    BOOST_CHECK_EQUAL(directory_count(fs->path("/")), 3);
    fs->path("/dir")->mkdir(0644);
    write_file(fs->path("/dir/foo"), "bar");
    BOOST_CHECK_THROW(fs->path("/test3")->rename("/dir"), rfs::Error);
    fs->path("/dir")->rename("/dir2");
    BOOST_CHECK_THROW(fs->path("/dir2")->rmdir(), rfs::Error);
    fs->path("/dir2/foo")->rename("/foo");
    fs->path("/dir2")->rmdir();
    fs->path("/foo")->unlink();
    fs->path("/test3")->unlink();
  }

  ELLE_LOG("cross-block")
  {
    auto h = fs->path("/babar")->create(O_RDWR|O_CREAT, 0644);
    const char* data = "abcdefghijklmnopqrstuvwxyz";
    auto res = h->write(elle::ConstWeakBuffer(data, strlen(data)), strlen(data),
      1024*1024-10);
    BOOST_CHECK_EQUAL(res, strlen(data));
    h->close();
    BOOST_CHECK_EQUAL(file_size(fs->path("/babar")), 1024 * 1024 - 10 + 26);
    h = fs->path("/babar")->open(O_RDONLY, 0666);
    char output[36];
    res = h->read(elle::WeakBuffer(output, 36), 36, 1024*1024 - 15);
    BOOST_CHECK_EQUAL(31, res);
    BOOST_CHECK_EQUAL(std::string(output+5, output+31),
                      data);
    BOOST_CHECK_EQUAL(std::string(output, output+31),
                      std::string(5, 0) + data);
    h->close();
    fs->path("/babar")->unlink();
  }

  ELLE_LOG("cross-block 2")
  {
    auto h = fs->path("/babar")->create(O_RDWR|O_CREAT, 0644);
    const char* data = "abcdefghijklmnopqrstuvwxyz";
    auto res = h->write(elle::ConstWeakBuffer(data, strlen(data)), strlen(data),
      1024*1024 + 16384 -10);
    BOOST_CHECK_EQUAL(res, strlen(data));
    h->close();
    BOOST_CHECK_EQUAL(file_size(fs->path("/babar")), 1024 * 1024 +16384 - 10 + 26);
    h = fs->path("/babar")->open(O_RDONLY, 0666);
    char output[36];
    res = h->read(elle::WeakBuffer(output, 36), 36, 1024*1024 + 16384 - 15);
    BOOST_CHECK_EQUAL(31, res);
    BOOST_CHECK_EQUAL(std::string(output+5, output+31),
                      data);
    BOOST_CHECK_EQUAL(std::string(output, output+31),
                      std::string(5, 0) + data);
    h->close();
    fs->path("/babar")->unlink();
  }

  ELLE_LOG("link/unlink")
  {
    fs->path("/u")->create(O_RDWR|O_CREAT, 0644);
    fs->path("/u")->unlink();
  }

  ELLE_LOG("multiple opens")
  {
    {
      write_file(fs->path("/test"), "Test");
      fs->path("/test")->open(O_RDWR, 0644);
    }
    BOOST_CHECK_EQUAL(read_file(fs->path("/test")), "Test");
    fs->path("/test")->unlink();
    {
      auto h = fs->path("/test")->create(O_CREAT|O_RDWR, 0644);
      h->write(elle::ConstWeakBuffer("Test", 4), 4, 0);
      {
        auto h2 = fs->path("/test")->open(O_RDWR, 0644);
        h2->close();
      }
      h->write(elle::ConstWeakBuffer("Test", 4), 4, 4);
      h->close();
    }
    BOOST_CHECK_EQUAL(read_file(fs->path("/test")), "TestTest");
    fs->path("/test")->unlink();
  }

  ELLE_LOG("randomizing a file")
  {
    auto const random_size = 10000;
    {
      auto h = fs->path("/tbig")->create(O_CREAT|O_RDWR, 0644);
      for (int i = 0; i < random_size; ++i)
      {
        unsigned char c = elle::pick_one(256);
        h->write(elle::ConstWeakBuffer(&c, 1), 1, i);
      }
      h->close();
    }
    BOOST_TEST(file_size(fs->path("/tbig")) == random_size);
    for (int i=0; i < 2; ++i)
    {
      auto h = fs->path("/tbig")->open(O_RDWR, 0644);
      for (int i=0; i < 5; ++i)
      {
        int sv = elle::pick_one(random_size);
        unsigned char c = elle::pick_one(256);
        h->write(elle::ConstWeakBuffer(&c, 1), 1, sv);
      }
      h->close();
    }
    BOOST_TEST(file_size(fs->path("/tbig")) == random_size);
  }

  ELLE_LOG("truncate")
  {
    auto p = fs->path("/tbig");
    p->truncate(9000000);
    read_all(p);
    p->truncate(8000000);
    read_all(p);
    p->truncate(5000000);
    read_all(p);
    p->truncate(2000000);
    read_all(p);
    p->truncate(900000);
    read_all(p);
    p->unlink();
  }

  ELLE_LOG("extended attributes")
  {
    fs->path("/")->setxattr("testattr", "foo", 0);
    write_file(fs->path("/file"), "test");
    fs->path("/file")->setxattr("testattr", "foo", 0);
    auto attrs = fs->path("/")->listxattr();
    BOOST_CHECK_EQUAL(attrs, std::vector<std::string>{"testattr"});
    attrs = fs->path("/file")->listxattr();
    BOOST_CHECK_EQUAL(attrs, std::vector<std::string>{"testattr"});
    BOOST_CHECK_EQUAL(fs->path("/")->getxattr("testattr"), "foo");
    BOOST_CHECK_EQUAL(fs->path("/file")->getxattr("testattr"), "foo");
    BOOST_CHECK_THROW(fs->path("/")->getxattr("nope"), rfs::Error);
    BOOST_CHECK_THROW(fs->path("/file")->getxattr("nope"), rfs::Error);
    fs->path("/file")->unlink();
  }

  ELLE_LOG("simultaneus read/write")
  {
    auto h = fs->path("/test")->create(O_RDWR|O_CREAT, 0644);
    char buf[1024];
    for (int i=0; i< 22 * 1024; ++i)
      h->write(elle::ConstWeakBuffer(buf, 1024), 1024, 1024*i);
    auto h2 = fs->path("/test")->open(O_RDONLY, 0644);
    h2->read(elle::WeakBuffer(buf, 1024), 1024, 0);
    h->write(elle::ConstWeakBuffer(buf, 1024), 1024, 1024*22*1024);
    h2->close();
    h->close();
    fs->path("/test")->unlink();
  }

  ELLE_LOG("symlink")
  {
    write_file(fs->path("/real_file"), "something");
    fs->path("/symlink")->symlink("/real_file");
    // Fuse handles symlinks for us
    //BOOST_CHECK_EQUAL(read_file(fs->path("/symlink")), "something");
    fs->path("/symlink")->unlink();
    fs->path("/real_file")->unlink();
  }

  ELLE_LOG("utf-8")
  {
    const char* name = "/éùßñЂ";
    write_file(fs->path(name), "foo");
    BOOST_CHECK_EQUAL(read_file(fs->path(name)), "foo");
    std::string sname;
    fs->path("/")->list_directory([&](std::string const& s, struct stat*) {
        sname = s;
    });
    BOOST_CHECK_EQUAL("/"+sname, name);
    BOOST_CHECK_EQUAL(directory_count(fs->path("/")), 3);
    fs->path(name)->unlink();
    BOOST_CHECK_EQUAL(directory_count(fs->path("/")), 2);
  }
}

#include <elle/Option.hh>

ELLE_TEST_SCHEDULED(upgrade_06_07)
{
  memo::silo::Memory::Blocks blocks;
  auto owner_key = elle::cryptography::rsa::keypair::generate(512);
  auto other_key = elle::cryptography::rsa::keypair::generate(512);
  auto other_key2 = elle::cryptography::rsa::keypair::generate(512);
  auto nid = memo::model::Address::random(0);
  char buf[1024];
  {
    auto dhts
      = DHTs(1, owner_key,
             keys = owner_key,
             storage = std::make_unique<memo::silo::Memory>(blocks),
             version = elle::Version(0,6,0),
             id = nid);
    auto client = dhts.client(false, {}, version = elle::Version(0, 6, 0));
    client.fs->path("/dir")->mkdir(0666);
    auto h = client.fs->path("/dir/file")->create(O_RDWR|O_CREAT, 0666);
    char buf[1024];
    for (int i=0; i<1200; ++i)
      h->write(elle::ConstWeakBuffer(buf, 1024), 1024, i * 1024);
    h->close();
    h.reset();
    client.fs->path("/dir/file")->setxattr("infinit.auth.setrw",
      elle::serialization::json::serialize(other_key.K()).string(), 0);
    client.fs->path("/dir/")->setxattr("infinit.auth.setrw",
      elle::serialization::json::serialize(other_key.K()).string(), 0);
    client.fs->path("/")->setxattr("infinit.auth.setrw",
      elle::serialization::json::serialize(other_key.K()).string(), 0);
  }
  {
    BOOST_CHECK(blocks.size());
    auto dhts
      = DHTs(1,
             owner_key,
             keys = owner_key,
             storage = std::make_unique<memo::silo::Memory>(blocks),
             version = elle::Version(0,7,0),
             dht::consensus::rebalance_auto_expand = false,
             id = nid);
    auto client = dhts.client(false);
    struct stat st;
    client.fs->path("/")->stat(&st);
    client.fs->path("/dir")->stat(&st);
    client.fs->path("/dir/file")->stat(&st);
    client.fs->path("/dir2")->mkdir(0666);
    auto h = client.fs->path("/dir/file2")->create(O_RDWR|O_CREAT, 0666);
    for (int i=0; i<1200; ++i)
      h->write(elle::ConstWeakBuffer(buf, 1024), 1024, i * 1024);
    h->close();
    h.reset();
    h = client.fs->path("/dir/file")->create(O_RDWR|O_CREAT, 0666);
    for (int i=0; i<1200; ++i)
      h->write(elle::ConstWeakBuffer(buf, 1024), 1024, i * 1024);
    h->close();
    h.reset();
    client.fs->path("/dir/file")->setxattr("infinit.auth.setr",
      elle::serialization::json::serialize(other_key.K()).string(), 0);
    client.fs->path("/dir/file")->setxattr("infinit.auth.setr",
      elle::serialization::json::serialize(other_key2.K()).string(), 0);
    auto client2 = dhts.client(false, other_key);
    client2.fs->path("/")->stat(&st);
    client2.fs->path("/dir")->stat(&st);
    client2.fs->path("/dir/file")->stat(&st);
  }
  {
    BOOST_CHECK(blocks.size());
    auto dhts
      = DHTs(1, owner_key,
             keys = owner_key,
             storage = std::make_unique<memo::silo::Memory>(blocks),
             version = elle::Version(0,7,0),
             dht::consensus::rebalance_auto_expand = false,
             id = nid);
    auto client = dhts.client(false);
    struct stat st;
    client.fs->path("/")->stat(&st);
    client.fs->path("/dir")->stat(&st);
    client.fs->path("/dir/file")->stat(&st);
    client.fs->path("/dir/file2")->stat(&st);
    client.fs->path("/dir2")->stat(&st);
    auto h = client.fs->path("/dir/file2")->open(O_RDONLY, 0666);
    BOOST_CHECK_EQUAL(1024, h->read(elle::WeakBuffer(buf, 1024), 1024, 0));
    h->close();
    h = client.fs->path("/dir/file")->open(O_RDONLY, 0666);
    BOOST_CHECK_EQUAL(1024, h->read(elle::WeakBuffer(buf, 1024), 1024, 0));
    h->close();
    auto client2 = dhts.client(false, other_key);
    client2.fs->path("/")->stat(&st);
    client2.fs->path("/dir")->stat(&st);
    client2.fs->path("/dir/file")->stat(&st);
  }
}

ELLE_TEST_SCHEDULED(conflicts)
{
  auto servers = DHTs(3, {}, dht::consensus_builder = no_cheat_consensus(), yielding_overlay = true);
  auto client0 = servers.client(false, {}, user_name="user0", yielding_overlay = true);
  auto& fs0 = client0.fs;
  auto client1 = servers.client(true, {}, user_name="user1", yielding_overlay = true);
  auto& fs1 = client1.fs;
  auto skey = serialize(client1.dht.dht->keys().K());
  fs0->path("/")->setxattr("infinit.auth.setrw", skey, 0);
  fs0->path("/")->setxattr("infinit.auth.inherit", "true", 0);
  // file create/write conflict
  auto h0 = fs0->path("/file")->create(O_CREAT|O_RDWR, 0600);
  auto h1 = fs1->path("/file")->create(O_CREAT|O_RDWR, 0600);
  h0->write(elle::ConstWeakBuffer("foo", 3), 3, 0);
  h1->write(elle::ConstWeakBuffer("bar", 3), 3, 0);
  h0->close();
  h1->close();
  h0.reset();
  h1.reset();
  BOOST_CHECK_EQUAL(read_file(fs0->path("/file")), "bar");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/file")), "bar");

  // directory conflict
  h0 = fs0->path("/file3")->create(O_CREAT|O_RDWR, 0600);
  h1 = fs1->path("/file4")->create(O_CREAT|O_RDWR, 0600);
  h0->close();
  h1->close();
  struct stat st;
  fs0->path("/file3")->stat(&st);
  fs1->path("/file4")->stat(&st);

  // write/replace
  /* No way in hell this can work...
  write_file(fs0->path("/file6"), "coin");
  write_file(fs1->path("/file6bis"), "nioc");
  h0 = fs0->path("/file6")->open(O_RDWR, 0600);
  h0->write(elle::ConstWeakBuffer("coin", 4), 4, 4);
  fs1->path("/file6bis")->rename("/files6");
  h0->close();
  BOOST_CHECK_EQUAL(read_file(fs0->path("/file6")), "nioc");
  BOOST_CHECK_EQUAL(read_file(fs1->path("/file6")), "nioc");
  */

  // create O_EXCL
  h0 = fs0->path("/file7")->create(O_CREAT|O_EXCL|O_RDWR, 0600);
  BOOST_CHECK_THROW(fs1->path("/file7")->create(O_CREAT|O_EXCL|O_RDWR, 0600), rfs::Error);
  h0->close();
}

ELLE_TEST_SCHEDULED(group_description)
{
  auto servers = DHTs(-1);
  auto owner = servers.client();
  auto member = servers.client(true);
  auto admin = servers.client(true);
  owner.fs->path("/");
  owner.fs->path("/")->setxattr("infinit.group.create", "grp", 0);
  auto const member_key =
    elle::serialization::json::serialize(member.dht.dht->keys().K()).string();
  owner.fs->path("/")->setxattr("infinit.group.add", "grp:" + member_key, 0);
  auto const admin_key =
    elle::serialization::json::serialize(admin.dht.dht->keys().K()).string();
  owner.fs->path("/")->setxattr(
    "infinit.group.addadmin", "grp:" + admin_key, 0);
  owner.fs->path("/")->setxattr("infinit.auth.setr", "@grp", 0);
  auto group_list = [&] (auto const& c) -> elle::json::Object
    {
      std::string str = c.fs->path("/")->getxattr("infinit.group.list.grp");
      std::stringstream ss(str);
      return boost::any_cast<elle::json::Object>(elle::json::read(ss));
    };
  auto set_description = [&] (auto const& c, std::string const& desc)
    {
      c.fs->path("/")->setxattr("infinit.groups.grp.description", desc, 0);
    };
  if (owner.dht.dht->version() < elle::Version(0, 8, 0))
  {
    BOOST_CHECK_THROW(set_description(owner, "blerg"), elle::Error);
  }
  else
  {
    // Check there is no description.
    BOOST_CHECK(!elle::contains(group_list(owner), "description"));
    // Admin adds a description.
    std::string description = "some generic description";
    set_description(owner, description);
    auto get_description = [&] (auto const& c) -> std::string
      {
        return c.fs->path("/")->getxattr("infinit.groups.grp.description");
      };
    // Check group members can see it.
    BOOST_CHECK_EQUAL(get_description(owner), description);
    BOOST_CHECK_EQUAL(get_description(member), description);
    BOOST_CHECK_EQUAL(get_description(admin), description);
    // Normal member can't change the description.
    BOOST_CHECK_THROW(set_description(member, "blerg"), elle::Exception);
    // Admin user can change the description.
    description = "42";
    set_description(admin, description);
    BOOST_CHECK_EQUAL(get_description(owner), description);
    // Unset the description.
    set_description(owner, "");
    BOOST_CHECK(!elle::contains(group_list(admin), "description"));
  }
}

ELLE_TEST_SCHEDULED(world_perm_conflict)
{
  auto servers = DHTs(1);
  auto client1 = servers.client(false, {}, with_cache = true);
  auto client2 = servers.client(false);
  auto kp = elle::cryptography::rsa::keypair::generate(512);
  auto skey = serialize(kp.K());
  write_file(client1.fs->path("/file"), "foo");
  client1.fs->path("/file")->setxattr("infinit.auth_others", "r", 0);
  struct stat st;
  client2.fs->path("/file")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 07, 4);
  // make one change with client2 to trigger conflict
  write_file(client2.fs->path("/file"), "foo", O_RDWR|O_TRUNC);
  client1.fs->path("/file")->setxattr("infinit.auth_others", "none", 0);
  client2.fs->path("/file")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 07, 0);

  // Test with a directory
  client1.fs->path("/dir")->mkdir(0666);
  client1.fs->path("/dir")->setxattr("infinit.auth_others", "r", 0);
  client2.fs->path("/dir")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 07, 4);
  // make change to trigger conflict
  write_file(client2.fs->path("/dir/foo"), "bar");
  client1.fs->path("/dir")->setxattr("infinit.auth_others", "none", 0);
  client2.fs->path("/dir")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 07, 0);

  // test with permissions on keys
  auto client3 = servers.client(true, kp);
  client1.fs->path("/")->setxattr("infinit.auth.setrw", skey, 0);
  client1.fs->path("/file")->setxattr("infinit.auth.setrw", skey, 0);
  client1.fs->path("/dir")->setxattr("infinit.auth.setrw", skey, 0);
  BOOST_CHECK_NO_THROW(read_file(client3.fs->path("/file")));
  BOOST_CHECK_NO_THROW(directory_count(client3.fs->path("/dir")));
  write_file(client2.fs->path("/file"), "bam", O_RDWR|O_TRUNC);
  client1.fs->path("/file")->setxattr("infinit.auth.clear", skey, 0);
  BOOST_CHECK_THROW(read_file(client3.fs->path("/file")), std::exception);
  write_file(client2.fs->path("/dir/foo2"), "bar");
  client1.fs->path("/dir")->setxattr("infinit.auth.clear", skey, 0);
  BOOST_CHECK_THROW(directory_count(client3.fs->path("/dir")), std::exception);
}

ELLE_TEST_SCHEDULED(world_perm_mode)
{
  auto servers = DHTs(1);
  auto client1 = servers.client(false, {});
  auto client2 = servers.client(true);
  auto client3 = DHTs::Client("volume", servers.dht(false, {}),
                              ifs::map_other_permissions = false);
  client1.fs->path("/");
  client1.fs->path("/")->chmod(0755);
  write_file(client1.fs->path("/foo"), "bar");
  struct stat st;
  client1.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 0);
  client3.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 0);

  client3.fs->path("/foo")->chmod(0644); // denied
  client1.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 0);
  client3.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 0);
  BOOST_CHECK_THROW(read_file(client2.fs->path("/foo")), std::exception);

  client3.fs->path("/foo")->chmod(0647);
  client1.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 1);
  client3.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 1);

  client1.fs->path("/foo")->chmod(0645);
  client1.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 5);
  client3.fs->path("/foo")->stat(&st);
  BOOST_CHECK_EQUAL(st.st_mode & 7, 5);
  BOOST_CHECK_NO_THROW(directory_count(client2.fs->path("/")));
  BOOST_CHECK_NO_THROW(read_file(client2.fs->path("/foo")));
}

void
read_unlink_test(int64_t size)
{
  auto servers = DHTs(1);
  auto client = servers.client();
  auto file = [&client]
    {
      ELLE_LOG_SCOPE("fetch file");
      return client.fs->path("/")->child("file");
    };
  ELLE_LOG("write test file");
  auto handle_w = file()->create(O_CREAT | O_RDWR, S_IFREG | 0644);
  std::string data(size, 'a');
  BOOST_CHECK_EQUAL(
    handle_w->write(elle::ConstWeakBuffer(data), size, 0), size);
  handle_w->close();
  handle_w.reset();
  // Open file handle twice, unlink and check that file is still accessible.
  ELLE_LOG("first open");
  auto handle_1 = file()->open(O_RDONLY, 0);
  ELLE_LOG("second open");
  auto handle_2 = file()->open(O_RDWR, 0);
  ELLE_LOG("unlink");
  file()->unlink();
  auto buffer = elle::Buffer(size);
  ELLE_LOG("read");
  BOOST_CHECK_EQUAL(handle_1->read(buffer, size, 0), size);
  BOOST_CHECK_EQUAL(buffer, data);
}

ELLE_TEST_SCHEDULED(read_unlink_small)
{
  ELLE_LOG("read_unlink_small");
  read_unlink_test(8);
}

ELLE_TEST_SCHEDULED(read_unlink_large)
{
  ELLE_LOG("read_unlink_large");
  read_unlink_test(10 * 1024);
}

ELLE_TEST_SCHEDULED(block_size)
{
  int kchunks = 5 * 1024;
  auto content = std::string(1024 * kchunks, 'a');
  for (unsigned int i=0; i<content.size(); ++i)
    content[i] = i % 199;
  auto check_file = [&](std::shared_ptr<elle::reactor::filesystem::Path> p) {
    auto h = p->open(O_RDONLY, 0644);
    char buf[1024];
    for (int i=0; i<kchunks; ++i)
    {
      if (h->read(elle::WeakBuffer(buf, 1024), 1024, i * 1024) != 1024)
      {
        ELLE_LOG("short read at %s", i);
        return false;
      }
      for (int o=0; o<1024; ++o)
        if ((unsigned char)buf[o] != (i * 1024 + o) % 199)
        {
          ELLE_LOG("bad data at %s,%s", i, o);
          return false;
        }
    }
    if (h->read(elle::WeakBuffer(buf, 1024), 1024, kchunks * 1024) > 0)
    {
      ELLE_LOG("extra data");
      return false;
    }
    return true;
  };
  auto write_file_h = [&](std::unique_ptr<elle::reactor::filesystem::Handle>& h) {
    for (int i=0; i<kchunks; ++i)
    {
      if (h->write(elle::ConstWeakBuffer(content.data() + i * 1024, 1024),
                   1024, i * 1024)
          != 1024)
        return false;
    }
    return true;
  };
  auto write_file = [&](std::shared_ptr<elle::reactor::filesystem::Path> p) {
     auto h = p->create(O_RDWR | O_CREAT | O_TRUNC, 0644);
     if (write_file_h(h))
     {
       h->close();
       return true;
     }
     else
       return false;
  };
  elle::ConstWeakBuffer cc(content.data(), content.size());
  auto servers = DHTs(3, {}, dht::consensus_builder = no_cheat_consensus(), yielding_overlay = true);
  auto client1 = servers.client(false, {}, yielding_overlay = true);
  auto client2 = servers.client(false, {}, yielding_overlay = true);
  BOOST_CHECK(write_file(client1.fs->path("/foo")));
  BOOST_CHECK(check_file(client1.fs->path("/foo")));
  BOOST_CHECK(check_file(client2.fs->path("/foo")));
  dynamic_cast<memo::filesystem::FileSystem*>(client1.fs->operations().get())
    ->block_size(2 * 1024 * 1024);
  BOOST_CHECK(check_file(client1.fs->path("/foo")));
  BOOST_CHECK(check_file(client2.fs->path("/foo")));
  BOOST_CHECK(write_file(client1.fs->path("/foo")));
  BOOST_CHECK(check_file(client1.fs->path("/foo")));
  BOOST_CHECK(check_file(client2.fs->path("/foo")));
  BOOST_CHECK(write_file(client2.fs->path("/foo")));
  BOOST_CHECK(check_file(client1.fs->path("/foo")));
  BOOST_CHECK(check_file(client2.fs->path("/foo")));
  // conflict
  auto h1 = client1.fs->path("/foo1")->create(O_RDWR | O_CREAT | O_TRUNC, 0644);
  auto h2 = client2.fs->path("/foo1")->create(O_RDWR | O_CREAT | O_TRUNC, 0644);
  BOOST_CHECK(write_file_h(h2));
  BOOST_CHECK(write_file_h(h1));
  h2->close();
  h1->close();
  h2.reset();
  h1.reset();
  BOOST_CHECK(check_file(client1.fs->path("/foo1")));
  BOOST_CHECK(check_file(client2.fs->path("/foo1")));
  // other way round
  h2 = client2.fs->path("/foo2")->create(O_RDWR | O_CREAT | O_TRUNC, 0644);
  h1 = client1.fs->path("/foo2")->create(O_RDWR | O_CREAT | O_TRUNC, 0644);
  BOOST_CHECK(write_file_h(h1));
  BOOST_CHECK(write_file_h(h2));
  h1->close();
  h2->close();
  h1.reset();
  h2.reset();
  BOOST_CHECK(check_file(client1.fs->path("/foo2")));
  BOOST_CHECK(check_file(client2.fs->path("/foo2")));
}

ELLE_TEST_SUITE()
{
  // This is needed to ignore child process exiting with nonzero
  // There is unfortunately no more specific way.
  elle::os::setenv("BOOST_TEST_CATCH_SYSTEM_ERRORS", "no");
#ifndef ELLE_WINDOWS
  signal(SIGCHLD, SIG_IGN);
#endif
  auto& suite = boost::unit_test::framework::master_test_suite();
  // Fast tests that do not mount
  suite.add(BOOST_TEST_CASE(write_read), 0, valgrind(1));
  suite.add(BOOST_TEST_CASE(basic), 0, valgrind(20));
  suite.add(BOOST_TEST_CASE(acls), 0, valgrind(20));
  suite.add(BOOST_TEST_CASE(write_unlink), 0, valgrind(1));
  suite.add(BOOST_TEST_CASE(write_truncate), 0, valgrind(1));
  suite.add(BOOST_TEST_CASE(prefetcher_failure), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(paxos_race), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(data_embed), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(symlink_perms), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(short_hash_key), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(rename_exceptions), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(erased_group), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(erased_group_recovery), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(remove_permissions), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(create_excl),0, valgrind(5));
  suite.add(BOOST_TEST_CASE(sparse_file),0, valgrind(5));
  suite.add(BOOST_TEST_CASE(upgrade_06_07),0, valgrind(5));
  suite.add(BOOST_TEST_CASE(create_race),0, valgrind(5));
  suite.add(BOOST_TEST_CASE(conflicts), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(group_description), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(world_perm_conflict), 0, valgrind(10));
  suite.add(BOOST_TEST_CASE(world_perm_mode), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(read_unlink_small), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(read_unlink_large), 0, valgrind(5));
  suite.add(BOOST_TEST_CASE(block_size), 0, valgrind(10));
}
