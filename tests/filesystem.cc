#include <dirent.h>
#include <errno.h>
#include <random>

#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef INFINIT_WINDOWS
# include <sys/statvfs.h>
#endif

#ifdef INFINIT_WINDOWS
#undef stat
#endif

#ifdef INFINIT_LINUX
# include <attr/xattr.h>
#elif defined(INFINIT_MACOSX)
# include <sys/xattr.h>
#endif

#include <boost/filesystem/fstream.hpp>

#include <elle/UUID.hh>
#include <elle/format/base64.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/system/Process.hh>
#include <elle/test.hh>
#include <elle/utils.hh>
#include <elle/Version.hh>

#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Cache.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/model/faith/Faith.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/utility.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("test");

namespace ifs = infinit::filesystem;
namespace rfs = reactor::filesystem;
namespace bfs = boost::filesystem;


#ifdef INFINIT_WINDOWS
#define O_CREAT _O_CREAT
#define O_RDWR _O_RDWR
#define O_EXCL _O_EXCL
#define S_IFREG _S_IFREG
#endif

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

std::vector<infinit::model::Address>
get_fat(std::string const& attr)
{
  std::stringstream input(attr);
  std::vector<infinit::model::Address> res;
  for (auto const& entry:
         boost::any_cast<elle::json::Array>(elle::json::read(input)))
    res.push_back(infinit::model::Address::from_string(
                    boost::any_cast<std::string>(entry)));
  return res;
}

class NoCheatConsensus: public infinit::model::doughnut::consensus::Consensus
{
public:
  typedef infinit::model::doughnut::consensus::Consensus Super;
  NoCheatConsensus(std::unique_ptr<Super> backend)
  : Super(backend->doughnut())
  , _backend(std::move(backend))
  {}
protected:
  virtual
  std::unique_ptr<infinit::model::doughnut::Local>
  make_local(boost::optional<int> port,
             boost::optional<boost::asio::ip::address> listen,
             std::unique_ptr<infinit::storage::Storage> storage,
             dht::Protocol p) override
  {
    return _backend->make_local(port, listen, std::move(storage), p);
  }

  virtual
  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(infinit::model::Address address, boost::optional<int> local_version) override
  {
    auto res = _backend->fetch(address, local_version);
    if (!res)
      return res;
    elle::Buffer buf;
    {
      elle::IOStream os(buf.ostreambuf());
      elle::serialization::binary::serialize(res, os);
    }
    elle::IOStream is(buf.istreambuf());
    elle::serialization::Context ctx;
    ctx.set(&doughnut());
    res = elle::serialization::binary::deserialize<std::unique_ptr<blocks::Block>>(
      is, true, ctx);
    return res;
  }
  virtual
  void
  _store(std::unique_ptr<infinit::model::blocks::Block> block,
    infinit::model::StoreMode mode,
    std::unique_ptr<infinit::model::ConflictResolver> resolver) override
  {
    this->_backend->store(std::move(block), mode, std::move(resolver));
  }
  virtual
  void
  _remove(infinit::model::Address address, infinit::model::blocks::RemoveSignature rs) override
  {
    if (rs.block)
    {
      elle::Buffer buf;
      {
        elle::IOStream os(buf.ostreambuf());
        elle::serialization::binary::serialize(rs.block, os);
      }
      elle::IOStream is(buf.istreambuf());
      elle::serialization::Context ctx;
      ctx.set(&doughnut());
      auto res = elle::serialization::binary::deserialize<std::unique_ptr<blocks::Block>>(
        is, true, ctx);
      rs.block = std::move(res);
    }
    _backend->remove(address, rs);
  }
  std::unique_ptr<Super> _backend;
};

std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
no_cheat_consensus(std::unique_ptr<infinit::model::doughnut::consensus::Consensus> c)
{
  return elle::make_unique<NoCheatConsensus>(std::move(c));
}

std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
same_consensus(std::unique_ptr<infinit::model::doughnut::consensus::Consensus> c)
{
  return c;
}

class DHTs
{
public:
  template <typename ... Args>
  DHTs(int count)
   : DHTs(count, {})
  {
  }
  template <typename ... Args>
  DHTs(int count,
       boost::optional<infinit::cryptography::rsa::KeyPair> kp,
       Args ... args)
    : owner_keys(kp? *kp : infinit::cryptography::rsa::keypair::generate(512))
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
    Client(std::string const& name, DHT dht)
      : dht(std::move(dht))
      , fs(elle::make_unique<reactor::filesystem::FileSystem>(
             elle::make_unique<infinit::filesystem::FileSystem>(
               name, this->dht.dht, ifs::allow_root_creation = true),
             true))
    {}

    DHT dht;
    std::unique_ptr<reactor::filesystem::FileSystem> fs;
  };

  template<typename... Args>
  Client
  client(bool new_key,
         boost::optional<infinit::cryptography::rsa::KeyPair> kp,
         Args... args)
  {
    auto k = kp ? *kp
        : new_key ? infinit::cryptography::rsa::keypair::generate(512)
          : this->owner_keys;
    ELLE_LOG("new client with owner=%f key=%f", this->owner_keys.K(), k.K());
    DHT client(owner = this->owner_keys,
               keys = k,
               storage = nullptr,
               make_consensus = no_cheat_consensus,
               paxos = pax,
               std::forward<Args>(args) ...
               );
    for (auto& dht: this->dhts)
      dht.overlay->connect(*client.overlay);
    return Client("volume", std::move(client));
  }

  Client
  client(bool new_key = false)
  {
    return client(new_key, {});
  }

  infinit::cryptography::rsa::KeyPair owner_keys;
  std::vector<DHT> dhts;
  bool pax;
};

ELLE_TEST_SCHEDULED(write_truncate)
{
  DHTs servers(1);
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
  DHTs servers(1);
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
    elle::Buffer b(5);
    BOOST_CHECK_EQUAL(handle->read(b, 5, 0), 5);
    BOOST_CHECK_EQUAL(b, "data1");
  }
  ELLE_LOG("remove file on client 2")
    root_2->child("file")->unlink();
  // Ability to read removed files not implemented yet, but it should
  // ELLE_LOG("read on client 1")
  // {
  //   elle::Buffer b(5);
  //   BOOST_CHECK_EQUAL(handle->read(b, 5, 0), 5);
  //   BOOST_CHECK_EQUAL(b, "data1");
  // }
  ELLE_LOG("write on client 1")
    BOOST_CHECK_EQUAL(handle->write(elle::ConstWeakBuffer("data2"), 5, 0), 5);
  ELLE_LOG("sync on client 1")
    handle->fsync(true);
  // ELLE_LOG("read on client 1")
  // {
  //   elle::Buffer b(5);
  //   BOOST_CHECK_EQUAL(handle->read(b, 5, 0), 5);
  //   BOOST_CHECK_EQUAL(b, "data2");
  // }
  ELLE_LOG("close file on client 1")
    BOOST_CHECK_NO_THROW(handle->close());
  ELLE_LOG("check file does not exist on client 2")
    BOOST_CHECK_THROW(root_1->child("file")->stat(&st), elle::Error);
  ELLE_LOG("check file does not exist on client 2")
    BOOST_CHECK_THROW(root_2->child("file")->stat(&st), elle::Error);
}

ELLE_TEST_SCHEDULED(prefetcher_failure)
{
  DHTs servers(1);
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
  reactor::sleep(200_ms);
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
  DHTs servers(1);
  auto c1 = servers.client();
  auto c2 = servers.client();
  auto r1 = c1.fs->path("/");
  auto r2 = c2.fs->path("/");
  ELLE_LOG("create both directories")
  {
    reactor::Thread t1("t1", [&] { r1->child("foo")->mkdir(0700);});
    reactor::Thread t2("t2", [&] { r2->child("bar")->mkdir(0700);});
    reactor::wait({t1, t2});
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
  DHTs servers(1);
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
  DHTs servers(-1);
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
  DHTs servers(1);
  auto client1 = servers.client();
  auto key = infinit::cryptography::rsa::keypair::generate(512);
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
  DHTs servers(-1);
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
  DHTs servers(-1);
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
  BOOST_CHECK_THROW(client1.fs->path("/dir2")->mkdir(0666), reactor::filesystem::Error);
  // we have inherit enabled, copy_permissions will fail on the missing group
  BOOST_CHECK_THROW(client2.fs->path("/dir/dir")->mkdir(0666), reactor::filesystem::Error);
  client2.fs->path("/file")->open(O_RDWR, 0644)->write(
    elle::ConstWeakBuffer("bar", 3), 3, 0);
}

ELLE_TEST_SCHEDULED(erased_group_recovery)
{
  DHTs servers(-1);
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
  DHTs servers(-1);
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
  DHTs servers(1, {}, with_cache = true);
  auto client1 = servers.client(false);
  auto client2 = servers.client(false);
  // cache feed
  client1.fs->path("/");
  client2.fs->path("/");
  client1.fs->path("/file")->create(O_RDWR|O_CREAT|O_EXCL, 0644);
  BOOST_CHECK_THROW(
    client2.fs->path("/file")->create(O_RDWR|O_CREAT|O_EXCL, 0644),
    reactor::filesystem::Error);
  // again, now that our cache knows the file
  BOOST_CHECK_THROW(
    client2.fs->path("/file")->create(O_RDWR|O_CREAT|O_EXCL, 0644),
    reactor::filesystem::Error);
}

ELLE_TEST_SCHEDULED(sparse_file)
{
  // Under windows, a 'cp' causes a ftruncate(target_size), so check that it
  // works
  DHTs servers(-1);
  auto client = servers.client();
  client.fs->path("/");
  for (int iter = 0; iter < 2; ++iter)
  { // run twice to get 'non-existing' and 'existing' initial states
    auto h = client.fs->path("/file")->create(O_RDWR | O_CREAT|O_TRUNC, 0666);
    char buf[191];
    char obuf[191];
    for (int i=0; i<191; ++i)
      buf[i] = i%191;
    int sz = 191 * (1 + 2500000/191);
    h->ftruncate(sz);
    for (int i=0;i<2500000; i+= 191)
    {
      h->write(elle::ConstWeakBuffer(buf, 191), 191, i);
    }
    h->close();
    h = client.fs->path("/file")->open(O_RDONLY, 0666);
    for (int i=0;i<2500000; i+= 191)
    {
      h->read(elle::WeakBuffer(obuf, 191), 191, i);
      BOOST_CHECK(!memcmp(obuf, buf, 191));
    }
  }
}

ELLE_TEST_SCHEDULED(create_race)
{
  DHTs dhts(3 /*, {},version = elle::Version(0, 6, 0)*/);
  auto client1 = dhts.client(false, {}, /*version = elle::Version(0,6,0),*/ yielding_overlay = true);
  auto client2 = dhts.client(false, {}, /*version = elle::Version(0,6,0),*/ yielding_overlay = true);
  client1.fs->path("/");
  reactor::Thread tpoll("poller", [&] {
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
        reactor::yield();
      }
  });
  reactor::yield();
  reactor::yield();
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
  reactor::wait(tpoll);
}

int write_file(std::shared_ptr<reactor::filesystem::Path> p,
               std::string const& content,
               int mode = O_CREAT|O_TRUNC|O_RDWR,
               int offset = 0)
{
  auto h = p->create(mode, 0666);
  auto sz = h->write(elle::ConstWeakBuffer(content.data(), content.size()), content.size(), offset);
  h->close();
  return sz;
}

std::string read_file(std::shared_ptr<reactor::filesystem::Path> p,
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

int directory_count(std::shared_ptr<reactor::filesystem::Path> p)
{
  int count = 0;
  p->list_directory([&count](std::string const&, struct stat*) {++count;});
  if (count == 0)
    throw rfs::Error(42, "directory listing error");
  return count;
}

size_t file_size(std::shared_ptr<reactor::filesystem::Path> p)
{
  struct stat st;
  p->stat(&st);
  return st.st_size;
}

void read_all(std::shared_ptr<reactor::filesystem::Path> p)
{
  auto h = p->open(O_RDONLY, 0644);
  char buf[16384];
  int i = 0;
  while (h->read(elle::WeakBuffer(buf, 16384), 16384, i*16384) == 16384)
    ++i;
}

ELLE_TEST_SCHEDULED(acls)
{
  DHTs servers(3, {}, make_consensus = no_cheat_consensus);
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
  auto sfat = fs0->path("/dirrm/rm3")->getxattr("infinit.fat");
  auto fat = get_fat(sfat);
  BOOST_CHECK_THROW(fs1->path("/dirrm")->setxattr("infinit.fsck.rmblock",
    elle::sprintf("%x", fat[0]), 0), rfs::Error);
  read_all(fs0->path("/dirrm/rm3"));
  fs0->path("/dirrm")->setxattr("infinit.fsck.rmblock",
    elle::sprintf("%x", fat[0]), 0);
  BOOST_CHECK_THROW(read_all(fs0->path("/dirrm/rm3")), rfs::Error);
}

ELLE_TEST_SCHEDULED(basic)
{
  DHTs servers(3);
  auto client = servers.client();
  auto& fs = client.fs;
  write_file(fs->path("/test"), "Test");
  BOOST_CHECK_EQUAL(read_file(fs->path("/test"), 4096), "Test");
  write_file(fs->path("/test"), "coin", O_WRONLY, 4);
  BOOST_CHECK_EQUAL(read_file(fs->path("/test"), 4096), "Testcoin");
  BOOST_CHECK_EQUAL(file_size(fs->path("/test")), 8);
  fs->path("/test")->unlink();

  BOOST_CHECK_THROW(read_file(fs->path("/foo")), rfs::Error);

  ELLE_LOG("truncate");
  {
    auto h = fs->path("/tt")->create(O_RDWR|O_TRUNC|O_CREAT, 0666);
    char buffer[16384];
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
  {
    ELLE_LOG("hard-link");
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

  {
    ELLE_LOG("Holes");
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
  {
    ELLE_LOG("use after unlink");
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
  {
    ELLE_LOG("rename");
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
  {
    ELLE_LOG("cross-block");
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
  {
    ELLE_LOG("cross-block 2");
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
  {
    ELLE_LOG("link/unlink");
    fs->path("/u")->create(O_RDWR|O_CREAT, 0644);
    fs->path("/u")->unlink();
  }
  {
    ELLE_LOG("multiple opens");
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
  {
    ELLE_LOG("randomizing a file");
    std::default_random_engine gen;
    std::uniform_int_distribution<>dist(0, 255);
    auto const random_size = 10000;
    {
      auto h = fs->path("/tbig")->create(O_CREAT|O_RDWR, 0644);
      for (int i = 0; i < random_size; ++i)
      {
        unsigned char c = dist(gen);
        h->write(elle::ConstWeakBuffer(&c, 1), 1, i);
      }
      h->close();
    }
    BOOST_CHECK_EQUAL(file_size(fs->path("/tbig")), random_size);
    std::uniform_int_distribution<>dist2(0, random_size - 1);
    for (int i=0; i < 2; ++i)
    {
      auto h = fs->path("/tbig")->open(O_RDWR, 0644);
      for (int i=0; i < 5; ++i)
      {
        int sv = dist2(gen);
        unsigned char c = dist(gen);
        h->write(elle::ConstWeakBuffer(&c, 1), 1, sv);
      }
      h->close();
    }
    BOOST_CHECK_EQUAL(file_size(fs->path("/tbig")), random_size);
  }
  {
    ELLE_LOG("truncate");
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
  {
    ELLE_LOG("extended attributes");
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
  {
    ELLE_LOG("simultaneus read/write");
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
  {
    ELLE_LOG("symlink");
    write_file(fs->path("/real_file"), "something");
    fs->path("/symlink")->symlink("/real_file");
    // Fuse handles symlinks for us
    //BOOST_CHECK_EQUAL(read_file(fs->path("/symlink")), "something");
    fs->path("/symlink")->unlink();
    fs->path("/real_file")->unlink();
  }
  {
    ELLE_LOG("utf-8");
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
  infinit::storage::Memory::Blocks blocks;
  auto owner_key = infinit::cryptography::rsa::keypair::generate(512);
  auto other_key = infinit::cryptography::rsa::keypair::generate(512);
  auto other_key2 = infinit::cryptography::rsa::keypair::generate(512);
  auto nid = infinit::model::Address::random(0);
  char buf[1024];
  {
    DHTs dhts(1, owner_key,
              keys = owner_key,
              storage = elle::make_unique<infinit::storage::Memory>(blocks),
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
    DHTs dhts(1, owner_key,
              keys = owner_key,
              storage = elle::make_unique<infinit::storage::Memory>(blocks),
              version = elle::Version(0,7,0),
              dht::consensus::rebalance_auto_expand = false,
              id = nid
              );
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
    DHTs dhts(1, owner_key,
              keys = owner_key,
              storage = elle::make_unique<infinit::storage::Memory>(blocks),
              version = elle::Version(0,7,0),
              dht::consensus::rebalance_auto_expand = false,
              id = nid
              );
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
  DHTs servers(3, {}, make_consensus = no_cheat_consensus, yielding_overlay = true);
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
  DHTs servers(-1);
  auto owner = servers.client();
  auto member = servers.client(true);
  auto admin = servers.client(true);
  owner.fs->path("/");
  owner.fs->path("/")->setxattr("infinit.group.create", "grp", 0);
  auto member_key =
    elle::serialization::json::serialize(member.dht.dht->keys().K()).string();
  owner.fs->path("/")->setxattr("infinit.group.add", "grp:" + member_key, 0);
  auto admin_key =
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
  // Check there is no description.
  BOOST_CHECK(group_list(owner).find("description") == group_list(owner).end());
  // Admin adds a description.
  std::string description = "some generic description";
  auto set_description = [&] (auto const& c, std::string const& desc)
    {
      c.fs->path("/")->setxattr("infinit.groups.grp.description", desc, 0);
    };
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
  BOOST_CHECK(group_list(admin).find("description") == group_list(admin).end());
}

ELLE_TEST_SCHEDULED(world_perm_conflict)
{
  DHTs servers(1);
  auto client1 = servers.client(false, {}, with_cache = true);
  auto client2 = servers.client(false);
  auto kp = infinit::cryptography::rsa::keypair::generate(512);
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

ELLE_TEST_SUITE()
{
  // This is needed to ignore child process exiting with nonzero
  // There is unfortunately no more specific way.
  elle::os::setenv("BOOST_TEST_CATCH_SYSTEM_ERRORS", "no", 1);
#ifndef INFINIT_WINDOWS
  signal(SIGCHLD, SIG_IGN);
#endif
  auto& suite = boost::unit_test::framework::master_test_suite();
  // Fast tests that do not mount
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
}
