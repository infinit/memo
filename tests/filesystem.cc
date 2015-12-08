#include <dirent.h>
#include <errno.h>
#include <random>

#include <sys/types.h>
#ifndef INFINIT_WINDOWS
#include <sys/statvfs.h>
#endif

#ifdef INFINIT_LINUX
#include <attr/xattr.h>
#elif defined(INFINIT_MACOSX)
#include <sys/xattr.h>
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
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/model/faith/Faith.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Memory.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/version.hh>

#ifdef INFINIT_MACOSX
# define SXA_EXTRA ,0
#else
# define SXA_EXTRA
#endif

ELLE_LOG_COMPONENT("test");

# define INFINIT_ELLE_VERSION elle::Version(INFINIT_MAJOR,   \
                                            INFINIT_MINOR,   \
                                            INFINIT_SUBMINOR)


namespace ifs = infinit::filesystem;
namespace rfs = reactor::filesystem;
namespace bfs = boost::filesystem;

bool mounted = false;
infinit::storage::Storage* storage;
reactor::filesystem::FileSystem* fs;
reactor::Scheduler* sched;

std::vector<std::string> mount_points;
std::vector<infinit::cryptography::rsa::PublicKey> keys;
std::vector<std::unique_ptr<infinit::model::doughnut::Doughnut>> nodes;
std::vector<boost::asio::ip::tcp::endpoint> endpoints;
infinit::overlay::Stonehenge::Peers peers;
std::vector<std::unique_ptr<elle::system::Process>> processes;

static void sig_int()
{
  fs->unmount();
}

static void group_create(bfs::path p, std::string const& name)
{
  setxattr(p.c_str(), "user.infinit.group.make", name.c_str(), name.size(), 0 SXA_EXTRA);
}
static void group_add(bfs::path p, std::string const& gname, std::string const& uname)
{
  std::string cmd = gname + ":" + uname;
  setxattr(p.c_str(), "user.infinit.group.add", cmd.c_str(), cmd.size(), 0 SXA_EXTRA);
}
static void group_remove(bfs::path p, std::string const& gname, std::string const& uname)
{
  std::string cmd = gname + ":" + uname;
  setxattr(p.c_str(), "user.infinit.group.remove", cmd.c_str(), cmd.size(), 0 SXA_EXTRA);
}
static void group_load(bfs::path p, std::string const& gname)
{
  setxattr(p.c_str(), ("user.infinit.group.load." + gname).c_str(), "", 0, 0
    SXA_EXTRA);
}

static void wait_for_mounts(boost::filesystem::path root, int count, struct statvfs* start = nullptr)
{
  struct statvfs stparent;
  if (start)
  {
    stparent = *start;
    ELLE_LOG("initializing with %s %s", stparent.f_fsid, stparent.f_blocks);
  }
  else
    statvfs(root.string().c_str(), &stparent);
  while (mount_points.size() < unsigned(count))
    usleep(20000);
#ifdef INFINIT_MACOSX
  // stat change monitoring does not work for unknown reasons
  usleep(2000000);
  return;
#endif
  struct statvfs st;
  for (int i=0; i<count; ++i)
  {
    while (true)
    {
      int res = statvfs(mount_points[i].c_str(), &st);
      ELLE_TRACE("%s fsid: %s %s  blk %s %s", i, st.f_fsid, stparent.f_fsid, st.f_blocks, stparent.f_blocks);
      // statvfs failure with EPERM means its mounted
      if (res < 0
        || st.f_fsid != stparent.f_fsid
        || st.f_blocks != stparent.f_blocks
        || st.f_bsize != stparent.f_bsize
        || st.f_flag != stparent.f_flag)
        break;
      usleep(20000);
    }
  }
}

static int directory_count(boost::filesystem::path const& p)
{
  try
  {
    boost::filesystem::directory_iterator d(p);
    int s=0;
    while (d!= boost::filesystem::directory_iterator())
    {
      ++s; ++d;
    }
    return s;
  }
  catch(std::exception const& e)
  {
    ELLE_LOG("directory_count failed with %s", e.what());
    return -1;
  }
}

static
bool
can_access(boost::filesystem::path const& p)
{
  struct stat st;
  if (stat(p.string().c_str(), &st) == -1)
  {
    BOOST_CHECK_EQUAL(errno, EACCES);
    return false;
  }
  if (S_ISDIR(st.st_mode))
  {
    auto dir = opendir(p.string().c_str());
    if (!dir)
      return false;
    else
    {
      auto ent = readdir(dir);
      auto e = errno;
      closedir(dir);
      return ent || e != EACCES;
    }
  }
  else
  {
    int fd = open(p.string().c_str(), O_RDONLY);
    if (fd < 0)
      return false;
    close(fd);
    return true;
  }
}

static bool touch(boost::filesystem::path const& p)
{
  boost::filesystem::ofstream ofs(p);
  if (!ofs.good())
    return false;
  ofs << "test";
  return true;
}

template<typename T> std::string serialize(T & t)
{
  elle::Buffer buf;
  {
    elle::IOStream ios(buf.ostreambuf());
    elle::serialization::json::SerializerOut so(ios, false);
    so.serialize_forward(t);
  }
  return buf.string();
}


// Run nodes in a separate scheduler to avoid reentrency issues
// ndmefyl: WHAT THE FUCK is that supposed to imply O.o
reactor::Scheduler* nodes_sched;
static void make_nodes(std::string store, int node_count,
                       infinit::cryptography::rsa::KeyPair const& owner,
                       bool paxos)
{
  reactor::Scheduler s;
  nodes_sched = &s;
  reactor::Thread t(s, "nodes", [&] {
    for (int i = 0; i < node_count; ++i)
      peers.emplace_back(infinit::model::Address::random());
    for (int i = 0; i < node_count; ++i)
    {
      // Create storage
      std::unique_ptr<infinit::storage::Storage> s;
      if (!elle::os::getenv("STORAGE_MEMORY", "").empty())
        s.reset(new infinit::storage::Memory());
      else
      {
        auto tmp = store / boost::filesystem::unique_path();
        std::cerr << i << " : " << tmp << std::endl;
        boost::filesystem::create_directories(tmp);
        s.reset(new infinit::storage::Filesystem(tmp));
      }
      auto kp = infinit::cryptography::rsa::keypair::generate(2048);
      infinit::model::doughnut::Passport passport(kp.K(), "testnet", owner.k());
      infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
        [paxos] (infinit::model::doughnut::Doughnut& dht)
        -> std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
        {
          if (paxos)
            return elle::make_unique<
              infinit::model::doughnut::consensus::Paxos>(dht, 3);
          else
            return elle::make_unique<
              infinit::model::doughnut::consensus::Consensus>(dht);
        };
      infinit::model::doughnut::Doughnut::OverlayBuilder overlay =
        [=] (infinit::model::doughnut::Doughnut& dht,
             infinit::model::Address id,
             std::shared_ptr<infinit::model::doughnut::Local> local)
        {
          return elle::make_unique<infinit::overlay::Stonehenge>(
            id, peers, std::move(local), &dht);
        };
      nodes.emplace_back(new infinit::model::doughnut::Doughnut(
                           peers[i].id,
                           std::make_shared<infinit::cryptography::rsa::KeyPair>(kp),
                           owner.K(),
                           passport,
                           consensus,
                           overlay,
                           boost::optional<int>(),
                           std::move(s),
                           INFINIT_ELLE_VERSION));
    }
    for (int i = 0; i < node_count; ++i)
      peers[i].endpoint = infinit::overlay::Stonehenge::Peer::Endpoint{
        "127.0.0.1", nodes[i]->local()->server_endpoint().port()};
    for (auto const& node: nodes)
      elle::unconst(static_cast<infinit::overlay::Stonehenge*>(
                      node->overlay().get())->peers()) = peers;

  });
  ELLE_LOG("Running node scheduler");
  s.run();
  ELLE_LOG("Exiting node scheduler");
}

static
void
run_filesystem_dht(std::string const& store,
                   std::string const& mountpoint,
                   int node_count,
                   int nread = 1,
                   int nwrite = 1,
                   int nmount = 1,
                   bool paxos = true)
{
  sched = new reactor::Scheduler();
  fs = nullptr;
  mount_points.clear();
  keys.clear();
  nodes.clear();
  endpoints.clear();
  processes.clear();
  peers.clear();
  mounted = false;
  auto owner_keys = infinit::cryptography::rsa::keypair::generate(2048);
  new std::thread([&] { make_nodes(store, node_count, owner_keys, paxos);});
  while (nodes.size() != unsigned(node_count))
    usleep(100000);
  ELLE_TRACE("got %s nodes, preparing %s mounts", nodes.size(), nmount);
  std::vector<reactor::Thread*> threads;
  reactor::Thread t(*sched, "fs", [&] {
    mount_points.reserve(nmount);
    for (int i=0; i< nmount; ++i)
    {
      std::string mp = mountpoint;
      if (nmount != 1)
      {
        mp = (mp / boost::filesystem::unique_path()).string();
      }
      mount_points.push_back(mp);
      boost::filesystem::create_directories(mp);
      if (nmount == 1)
      {
        ELLE_TRACE("configuring mounter...");
        //auto kp = infinit::cryptography::rsa::keypair::generate(2048);
        //keys.push_back(kp.K());
        keys.push_back(owner_keys.K());
        infinit::model::doughnut::Passport passport(owner_keys.K(), "testnet", owner_keys.k());
        ELLE_TRACE("instantiating dougnut...");
        infinit::model::doughnut::Doughnut::ConsensusBuilder consensus =
          [paxos] (infinit::model::doughnut::Doughnut& dht)
          -> std::unique_ptr<infinit::model::doughnut::consensus::Consensus>
          {
            if (paxos)
              return elle::make_unique<
            infinit::model::doughnut::consensus::Paxos>(dht, 3);
            else
              return elle::make_unique<
            infinit::model::doughnut::consensus::Consensus>(dht);
          };
        infinit::model::doughnut::Doughnut::OverlayBuilder overlay =
          [=] (infinit::model::doughnut::Doughnut& dht,
               infinit::model::Address id,
               std::shared_ptr<infinit::model::doughnut::Local> local)
          {
            return elle::make_unique<infinit::overlay::Stonehenge>(
              std::move(id), peers, std::move(local), &dht);
          };
        std::unique_ptr<infinit::model::Model> model =
        elle::make_unique<infinit::model::doughnut::Doughnut>(
          infinit::model::Address::random(),
          "testnet",
          std::make_shared<infinit::cryptography::rsa::KeyPair>(owner_keys),
          owner_keys.K(),
          passport,
          consensus,
          overlay,
          boost::optional<int>(),
          nullptr,
          INFINIT_ELLE_VERSION);
        ELLE_TRACE("instantiating ops...");
        std::unique_ptr<ifs::FileSystem> ops;
        ops = elle::make_unique<ifs::FileSystem>("default-volume", std::move(model));
        ELLE_TRACE("instantiating fs...");
        fs = new reactor::filesystem::FileSystem(std::move(ops), true);
        ELLE_TRACE("running mounter...");
        new reactor::Thread("mounter", [mp] {
            ELLE_LOG("mounting on %s", mp);
            mounted = true;
            fs->mount(mp, {"", "-o", "hard_remove"}); // {"", "-d" /*, "-o", "use_ino"*/});
            ELLE_TRACE("waiting...");
            reactor::wait(*fs);
            ELLE_TRACE("...done");
#ifndef INFINIT_MACOSX
            ELLE_LOG("filesystem unmounted");
            nodes_sched->mt_run<void>("clearer", [] { nodes.clear();});
            processes.clear();
            reactor::scheduler().terminate();
#endif
            });
      }
      else
      {
        // Having more than one mount in the same process is failing
        // Make a config file.
        elle::json::Object r;
        r["single_mount"] = false;
        r["mountpoint"] = mp;
        elle::json::Object model;
        model["type"] = "doughnut";
        model["name"] = "user" + std::to_string(i);
        auto kp = infinit::cryptography::rsa::keypair::generate(2048);
        keys.push_back(kp.K());
        model["id"] = elle::format::base64::encode(
          elle::ConstWeakBuffer(
            infinit::model::Address::random().value(),
            sizeof(infinit::model::Address::Value))).string();

        model["keys"] = "@KEYS@"; // placeholder, lolilol
        model["passport"] = "@PASSPORT@"; // placeholder, lolilol
        model["owner"] = "@OWNER@"; // placeholder, lolilol
        {
          elle::json::Object consensus;
          if (paxos)
          {
            consensus["type"] = "paxos";
            consensus["replication-factor"] = 3;
          }
          else
          {
            consensus["type"] = "single";
          }
          model["consensus"] = std::move(consensus);
        }
        {
          elle::json::Object overlay;
          overlay["type"] = "stonehenge";
          elle::json::Array v;
          for (auto const& p: peers)
          {
            elle::json::Object po;
            po["host"] = p.endpoint->host;
            po["port"] = p.endpoint->port;
            po["id"] = elle::format::base64::encode(
              elle::ConstWeakBuffer(
                p.id.value(),
                sizeof(infinit::model::Address::Value))).string();
            v.push_back(po);
          }
          overlay["peers"] = v;
          model["overlay"] = std::move(overlay);
        }
        r["model"] = model;
        std::string kps;
        if (i == 0)
          kps = serialize(owner_keys);
        else
          kps = serialize(kp);
        std::string owner_ser = serialize(owner_keys.K());
        infinit::model::doughnut::Passport passport(
          i == 0 ? owner_keys.K() : kp.K(), "testnet", owner_keys.k());
        std::string passport_ser = serialize(passport);
        std::stringstream ss;
        elle::json::write(ss, r, true);
        std::string ser = ss.str();
        // Now replace placeholder with key
        size_t pos = ser.find("\"@KEYS@\"");
        ser = ser.substr(0, pos) + kps + ser.substr(pos + 8);
        pos = ser.find("\"@PASSPORT@\"");
        ser = ser.substr(0, pos) + passport_ser + ser.substr(pos + 12);
        pos = ser.find("\"@OWNER@\"");
        ser = ser.substr(0, pos) + owner_ser + ser.substr(pos + 9);
        {
          std::ofstream ofs(mountpoint + "/" + std::to_string(i));
          ofs.write(ser.data(), ser.size());
        }
        std::vector<std::string> args {
          "bin/infinit",
          "-c",
          (mountpoint + "/" + std::to_string(i))
        };
        processes.emplace_back(new elle::system::Process(args));
        reactor::sleep(1_sec);
      }
    }
  });
  ELLE_TRACE("sched running");
  sched->run();
  ELLE_TRACE("sched exiting");
#ifdef INFINIT_MACOSX
  if (nmount == 1)
  {
    ELLE_LOG("filesystem unmounted");
    nodes_sched->mt_run<void>("clearer", [] { nodes.clear();});
    processes.clear();
  }
#endif

}

static void run_filesystem(std::string const& store, std::string const& mountpoint)
{
  sched = new reactor::Scheduler();
  fs = nullptr;
  mount_points.clear();
  keys.clear();
  nodes.clear();
  endpoints.clear();
  processes.clear();
  mounted = false;
  auto tmp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  std::unique_ptr<infinit::model::Model> model;
  reactor::Thread t(*sched, "fs", [&] {
    if (!elle::os::getenv("STORAGE_MEMORY", "").empty())
      storage = new infinit::storage::Memory();
    else
      storage = new infinit::storage::Filesystem(store);
    model = elle::make_unique<infinit::model::faith::Faith>(
      std::unique_ptr<infinit::storage::Storage>(storage),
      INFINIT_ELLE_VERSION);
    std::unique_ptr<ifs::FileSystem> ops = elle::make_unique<ifs::FileSystem>(
      "default-volume", std::move(model));
    fs = new reactor::filesystem::FileSystem(std::move(ops), true);
    mount_points.push_back(mountpoint);
    mounted = true;
    fs->mount(mountpoint, {"", "-ohard_remove"}); // {"", "-d" /*, "-o", "use_ino"*/});
    reactor::wait(*fs);
  });
  sched->run();
}

static std::string read(boost::filesystem::path const& where)
{
  std::string text;
  boost::filesystem::ifstream ifs(where);
  ifs >> text;
  return text;
}

static void read_all(boost::filesystem::path const& where)
{
  boost::filesystem::ifstream ifs(where);
  char buffer[1024];
  while (true)
  {
    ifs.read(buffer, 1024);
    if (!ifs.gcount())
      return;
  }
}

static void write(boost::filesystem::path const& where, std::string const& what)
{
  boost::filesystem::ofstream ofs(where);
  ofs << what;
}

void
test_filesystem(bool dht,
                int nnodes = 5,
                int nread = 1,
                int nwrite = 1,
                bool paxos = true)
{
  namespace bfs = boost::filesystem;
  auto store = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  auto mount = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  boost::filesystem::create_directories(mount);
  boost::filesystem::create_directories(store);
  mount_points.clear();
  struct statvfs statstart;
  statvfs(mount.string().c_str(), &statstart);
  std::thread t([&] {
      if (dht)
        run_filesystem_dht(store.string(), mount.string(), 5,
                           nread, nwrite, 1, paxos);
      else
        run_filesystem(store.string(), mount.string());
  });
  wait_for_mounts(mount, 1, &statstart);
  ELLE_LOG("starting test, mnt=%s, store=%s", mount, store);

  elle::SafeFinally remover([&] {
      ELLE_TRACE("unmounting");
      //fs->unmount();
      try
      {
        sched->mt_run<void>("unmounter", [&] { fs->unmount();});
      }
      catch(std::exception const& e)
      {
        ELLE_TRACE("unmounter threw %s", e.what());
      }
      //reactor::Thread th(*sched, "unmount", [&] { fs->unmount();});
      t.join();
      ELLE_TRACE("cleaning up");
      for (auto const& mp: mount_points)
      {
        std::vector<std::string> args
#ifdef INFINIT_MACOSX
          {"umount", "-f", mp};
#else
          {"fusermount", "-u", mp};
#endif
        elle::system::Process p(args);
      }
      try
      {
        usleep(200000);
        boost::filesystem::remove_all(store);
        ELLE_TRACE("remove mount");
        boost::filesystem::remove_all(mount);
        ELLE_TRACE("Cleaning done");
      }
      catch (std::exception const& e)
      {
        ELLE_TRACE("Exception cleaning up: %s", e.what());
      }
  });
  std::string text;

  {
    boost::filesystem::ofstream ofs(mount / "test");
    ofs << "Test";
  }
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

  char buffer[16384];
  //truncate
  {
    bfs::ofstream ofs(mount / "tt");
    for (int i=0; i<100; ++i)
      ofs.write(buffer, 16384);
  }
  int tfd = open( (mount / "tt").c_str(), O_RDWR);
  ftruncate(tfd, 0);
  write(tfd, buffer, 16384);
  write(tfd, buffer, 12288);
  write(tfd, buffer, 3742);
  ftruncate(tfd, 32414);
  ftruncate(tfd, 32413);
  close(tfd);
  BOOST_CHECK_EQUAL(bfs::file_size(mount / "tt"), 32413);

  bfs::remove(mount / "tt");
  struct stat st;
  // hardlink
#ifndef INFINIT_MACOSX
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
  // XXX [@Matthieu]: Should be 500000.
  usleep(750000);
  stat((mount / "test").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 14);
  stat((mount / "test2").string().c_str(), &st);
  BOOST_CHECK_EQUAL(st.st_size, 14);
  text = read(mount / "test");
  BOOST_CHECK_EQUAL(text, "TestcoinBcoinA");
  text = read(mount / "test2");
  BOOST_CHECK_EQUAL(text, "TestcoinBcoinA");
  bfs::remove(mount / "test");
  text = read(mount / "test2");
  BOOST_CHECK_EQUAL(text, "TestcoinBcoinA");
  bfs::remove(mount / "test2");

  // hardlink opened handle
  {
    bfs::ofstream ofs(mount / "test");
    ofs << "Test";
  }
  {
    bfs::ofstream ofs(mount / "test", std::ofstream::out|std::ofstream::ate|std::ofstream::app);
    ofs << "a";
    bfs::create_hard_link(mount / "test", mount / "test2");
    ofs << "b";
    ofs.close();
    text = read(mount / "test");
    BOOST_CHECK_EQUAL(text, "Testab");
    text = read(mount / "test2");
    BOOST_CHECK_EQUAL(text, "Testab");
    bfs::remove(mount / "test");
    bfs::remove(mount / "test2");
  }
#endif

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

  ELLE_LOG("test use after unlink")
  {
    fd = open((mount / "test").string().c_str(), O_RDWR|O_CREAT, 0644);
    if (fd < 0)
      perror("open");
    ELLE_LOG("write initial data")
      ELLE_ENFORCE_EQ(write(fd, "foo", 3), 3);
    ELLE_LOG("unlink")
      bfs::remove(mount / "test");
    ELLE_LOG("write additional data")
    {
      int res = write(fd, "foo", 3);
      BOOST_CHECK_EQUAL(res, 3);
    }
    ELLE_LOG("reread data")
    {
      lseek(fd, 0, SEEK_SET);
      char buf[7] = {0};
      int res = read(fd, buf, 6);
      BOOST_CHECK_EQUAL(res, 6);
      BOOST_CHECK_EQUAL(buf, "foofoo");
    }
    close(fd);
    BOOST_CHECK_EQUAL(directory_count(mount), 0);
  }

  ELLE_LOG("test rename")
  {
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
    bfs::remove(mount / "test3");
  }

  ELLE_LOG("test cross-block")
  {
    fd = open((mount / "babar").string().c_str(), O_RDWR|O_CREAT, 0644);
    BOOST_CHECK_GE(fd, 0);
    lseek(fd, 1024*1024 - 10, SEEK_SET);
    const char* data = "abcdefghijklmnopqrstuvwxyz";
    int res = write(fd, data, strlen(data));
    BOOST_CHECK_EQUAL(res, strlen(data));
    close(fd);
    stat((mount / "babar").string().c_str(), &st);
    BOOST_CHECK_EQUAL(st.st_size, 1024 * 1024 - 10 + 26);
    char output[36];
    fd = open((mount / "babar").string().c_str(), O_RDONLY);
    BOOST_CHECK_GE(fd, 0);
    lseek(fd, 1024*1024 - 15, SEEK_SET);
    res = read(fd, output, 36);
    BOOST_CHECK_EQUAL(31, res);
    BOOST_CHECK_EQUAL(std::string(output+5, output+31),
                      data);
    BOOST_CHECK_EQUAL(std::string(output, output+31),
                      std::string(5, 0) + data);
    close(fd);
    bfs::remove(mount / "babar");
  }
  ELLE_LOG("test cross-block 2")
  {
    fd = open((mount / "bibar").string().c_str(), O_RDWR|O_CREAT, 0644);
    BOOST_CHECK_GE(fd, 0);
    lseek(fd, 1024*1024 + 16384 - 10, SEEK_SET);
    const char* data = "abcdefghijklmnopqrstuvwxyz";
    int res = write(fd, data, strlen(data));
    BOOST_CHECK_EQUAL(res, strlen(data));
    close(fd);
    stat((mount / "bibar").string().c_str(), &st);
    BOOST_CHECK_EQUAL(st.st_size, 1024 * 1024 +16384 - 10 + 26);
    char output[36];
    fd = open((mount / "bibar").string().c_str(), O_RDONLY);
    BOOST_CHECK_GE(fd, 0);
    lseek(fd, 1024*1024 +16384 - 15, SEEK_SET);
    res = read(fd, output, 36);
    BOOST_CHECK_EQUAL(31, res);
    BOOST_CHECK_EQUAL(std::string(output+5, output+31),
                      data);
    BOOST_CHECK_EQUAL(std::string(output, output+31),
                      std::string(5, 0) + data);
    close(fd);
    bfs::remove(mount / "bibar");
  }

  ELLE_LOG("test link/unlink")
  {
    fd = open((mount / "u").string().c_str(), O_RDWR|O_CREAT, 0644);
    ::close(fd);
    bfs::remove(mount / "u");
  }

  ELLE_LOG("test multiple open, but with only one open")
  {
    {
      boost::filesystem::ofstream ofs(mount / "test");
      ofs << "Test";
    }
    BOOST_CHECK_EQUAL(read(mount / "test"), "Test");
    bfs::remove(mount / "test");
  }

  ELLE_LOG("test multiple opens")
  {
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
  }

  ELLE_LOG("test randomizing a file");
  {
    // randomized manyops
    std::default_random_engine gen;
    std::uniform_int_distribution<>dist(0, 255);
    {
      boost::filesystem::ofstream ofs(mount / "tbig");
      for (int i=0; i<10000000; ++i)
        ofs.put(dist(gen));
    }
    usleep(1000000);
    ELLE_TRACE("random writes");
    BOOST_CHECK_EQUAL(boost::filesystem::file_size(mount / "tbig"), 10000000);
    std::uniform_int_distribution<>dist2(0, 9999999);
    for (int i=0; i < (dht?1:10); ++i)
    {
      if (! (i%10))
        ELLE_TRACE("Run %s", i);
      ELLE_TRACE("opening");
      int fd = open((mount / "tbig").string().c_str(), O_RDWR);
      for (int i=0; i < 5; ++i)
      {
        int sv = dist2(gen);
        lseek(fd, sv, SEEK_SET);
        unsigned char c = dist(gen);
        ELLE_TRACE("Write 1 at %s", sv);
        write(fd, &c, 1);
      }
      ELLE_TRACE("Closing");
      close(fd);
    }
    ELLE_TRACE("truncates");
    BOOST_CHECK_EQUAL(boost::filesystem::file_size(mount / "tbig"), 10000000);
  }

  ELLE_LOG("test truncate")
  {
    ELLE_TRACE("truncate 9");
    boost::filesystem::resize_file(mount / "tbig", 9000000);
    read_all(mount / "tbig");
    ELLE_TRACE("truncate 8");
    boost::filesystem::resize_file(mount / "tbig", 8000000);
    read_all(mount / "tbig");
    ELLE_TRACE("truncate 5");
    boost::filesystem::resize_file(mount / "tbig", 5000000);
    read_all(mount / "tbig");
    ELLE_TRACE("truncate 2");
    boost::filesystem::resize_file(mount / "tbig", 2000000);
    read_all(mount / "tbig");
    ELLE_TRACE("truncate .9");
    boost::filesystem::resize_file(mount / "tbig", 900000);
    read_all(mount / "tbig");
    bfs::remove(mount / "tbig");
  }

  ELLE_LOG("test extended attributes")
  {
    setxattr(mount.c_str(), "testattr", "foo", 3, 0 SXA_EXTRA);
    touch(mount / "file");
    setxattr((mount / "file").c_str(), "testattr", "foo", 3, 0 SXA_EXTRA);
    char attrlist[1024];
    ssize_t sz = listxattr(mount.c_str(), attrlist, 1024 SXA_EXTRA);
    BOOST_CHECK_EQUAL(sz, strlen("testattr")+1);
    BOOST_CHECK_EQUAL(attrlist, "testattr");
    sz = listxattr( (mount / "file").c_str(), attrlist, 1024 SXA_EXTRA);
    BOOST_CHECK_EQUAL(sz, strlen("testattr")+1);
    BOOST_CHECK_EQUAL(attrlist, "testattr");
    sz = getxattr(mount.c_str(), "testattr", attrlist, 1024 SXA_EXTRA SXA_EXTRA);
    BOOST_CHECK_EQUAL(sz, strlen("foo"));
    attrlist[sz] = 0;
    BOOST_CHECK_EQUAL(attrlist, "foo");
    sz = getxattr( (mount / "file").c_str(), "testattr", attrlist, 1024 SXA_EXTRA SXA_EXTRA);
    BOOST_CHECK_EQUAL(sz, strlen("foo"));
    attrlist[sz] = 0;
    BOOST_CHECK_EQUAL(attrlist, "foo");
    sz = getxattr( (mount / "file").c_str(), "nope", attrlist, 1024 SXA_EXTRA SXA_EXTRA);
    BOOST_CHECK_EQUAL(sz, -1);
    sz = getxattr( (mount / "nope").c_str(), "nope", attrlist, 1024 SXA_EXTRA SXA_EXTRA);
    BOOST_CHECK_EQUAL(sz, -1);
    sz = getxattr( mount.c_str(), "nope", attrlist, 1024 SXA_EXTRA SXA_EXTRA);
    BOOST_CHECK_EQUAL(sz, -1);
    bfs::remove(mount / "file");
  }
  ELLE_LOG("simultaneus read/write");
  {
    bfs::ofstream ofs(mount / "test");
    char buf[1024];
    // write enough data so that the read will cause a cache eviction
    for (int i=0; i< 22 * 1024; ++i)
      ofs.write(buf, 1024);
    bfs::ifstream ifs(mount / "test");
    ifs.read(buf, 1024);
    ofs.write(buf, 1024);
    ifs.close();
    ofs.close();
    bfs::remove(mount / "test");
  }

  ELLE_LOG("utf-8");
  const char* name = "éùßñЂ";
  write(mount / name, "foo");
  BOOST_CHECK_EQUAL(read(mount / name), "foo");
  BOOST_CHECK_EQUAL(directory_count(mount), 1);
  bfs::directory_iterator it(mount);
  BOOST_CHECK_EQUAL(it->path().filename(), name);
  BOOST_CHECK_EQUAL(it->path().filename(), std::string(name));
  bfs::remove(mount / name);
  BOOST_CHECK_EQUAL(directory_count(mount), 0);
}

void test_basic()
{
  test_filesystem(false);
}

void filesystem()
{
  test_filesystem(true, 5, 1, 1, false);
}

void filesystem_paxos()
{
  test_filesystem(true, 5, 1, 1, true);
}

void unmounter(boost::filesystem::path mount,
               boost::filesystem::path store,
               std::thread& t)
{
  ELLE_LOG("unmounting");
  if (!nodes_sched->done())
    nodes_sched->mt_run<void>("clearer", [] { nodes.clear();});
  ELLE_LOG("cleaning up: TERM %s", processes.size());
  for (auto const& p: processes)
    kill(p->pid(), SIGTERM);
  usleep(200000);
  ELLE_LOG("cleaning up: KILL");
  for (auto const& p: processes)
    kill(p->pid(), SIGKILL);
  usleep(200000);
  // unmount all
  for (auto const& mp: mount_points)
  {
    std::vector<std::string> args
#ifdef INFINIT_MACOSX
    {"umount", mp};
#else
    {"fusermount", "-u", mp};
#endif
    elle::system::Process p(args);
  }
  usleep(200000);
  boost::filesystem::remove_all(mount);
  boost::filesystem::remove_all(store);
  t.join();
  ELLE_LOG("teardown complete");
}

void
test_conflicts(bool paxos)
{
  namespace bfs = boost::filesystem;
  auto store = bfs::temp_directory_path() / bfs::unique_path();
  auto mount = bfs::temp_directory_path() / bfs::unique_path();
  bfs::create_directories(mount);
  bfs::create_directories(store);
  struct statvfs statstart;
  statvfs(mount.string().c_str(), &statstart);
  mount_points.clear();
  std::thread t([&] {
      run_filesystem_dht(store.string(), mount.string(), 5, 1, 1, 2, paxos);
  });
  wait_for_mounts(mount, 2, &statstart);
  elle::SafeFinally remover([&] {
      try
      {
        unmounter(mount, store, t);
      }
      catch (std::exception const& e)
      {
        ELLE_TRACE("unmounter threw %s", e.what());
      }
  });
  // Mounts/keys are in mount_points and keys
  // First entry got the root!
  BOOST_CHECK_EQUAL(mount_points.size(), 2);
  bfs::path m0 = mount_points[0];
  bfs::path m1 = mount_points[1];
  BOOST_CHECK_EQUAL(keys.size(), 2);
  std::string k1 = serialize(keys[1]);
  ELLE_LOG("set permissions");
  {
    setxattr(m0.c_str(), "user.infinit.auth.setrw",
             k1.c_str(), k1.length(), 0 SXA_EXTRA);
    setxattr(m0.c_str(), "user.infinit.auth.inherit",
           "true", strlen("true"), 0 SXA_EXTRA);
  }
  ELLE_LOG("file create/write conflict")
  {
    int fd0, fd1;
    fd0 = open((m0 / "file").string().c_str(), O_CREAT|O_RDWR, 0644);
    BOOST_CHECK(fd0 != -1);
    fd1 = open((m1 / "file").string().c_str(), O_CREAT|O_RDWR, 0644);
    BOOST_CHECK(fd1 != -1);
    BOOST_CHECK_EQUAL(write(fd0, "foo", 3), 3);
    BOOST_CHECK_EQUAL(write(fd1, "bar", 3), 3);
    BOOST_CHECK_EQUAL(close(fd0), 0);
    BOOST_CHECK_EQUAL(close(fd1), 0);
    BOOST_CHECK_EQUAL(read(m0/"file"), "bar");
    BOOST_CHECK_EQUAL(read(m1/"file"), "bar");
  }
  // FIXME: This needs cache to be enabled ; restore when cache is moved up to
  // Model instead of the consensus and the 'infinit' binary accepts --cache.
  // ELLE_LOG("file create/write without acl inheritance")
  // {
  //   int fd0, fd1;
  //   setxattr(m0.c_str(), "user.infinit.auth.inherit",
  //            "false", strlen("false"), 0 SXA_EXTRA);
  //   // Force caching of '/' in second mount, otherwise if it fetches,
  //   // it will see the new 'file2' and fail the create.
  //   struct stat st;
  //   stat(m1.string().c_str(), &st);
  //   fd0 = open((m0 / "file2").string().c_str(), O_CREAT|O_RDWR, 0644);
  //   BOOST_CHECK(fd0 != -1);
  //   fd1 = open((m1 / "file2").string().c_str(), O_CREAT|O_RDWR, 0644);
  //   BOOST_CHECK(fd1 != -1);
  //   BOOST_CHECK_EQUAL(write(fd0, "foo", 3), 3);
  //   BOOST_CHECK_EQUAL(write(fd1, "bar", 3), 3);
  //   BOOST_CHECK_EQUAL(close(fd0), 0);
  //   BOOST_CHECK_EQUAL(close(fd1), 0);
  //   BOOST_CHECK_EQUAL(read(m1/"file"), "bar");
  // }
  struct stat st;
  ELLE_LOG("directory conflict")
  {
    int fd0, fd1;
    // force file node into filesystem cache
    stat((m0/"file3").c_str(), &st);
    stat((m1/"file4").c_str(), &st);
    fd0 = open((m0 / "file3").string().c_str(), O_CREAT|O_RDWR, 0644);
    BOOST_CHECK(fd0 != -1);
    fd1 = open((m1 / "file4").string().c_str(), O_CREAT|O_RDWR, 0644);
    BOOST_CHECK(fd1 != -1);
    BOOST_CHECK_EQUAL(close(fd0), 0);
    BOOST_CHECK_EQUAL(close(fd1), 0);
    BOOST_CHECK_EQUAL(stat((m0/"file3").c_str(), &st), 0);
    BOOST_CHECK_EQUAL(stat((m1/"file4").c_str(), &st), 0);
  }
  ELLE_LOG("write/unlink")
  {
    int fd0;
    setxattr(m0.c_str(), "user.infinit.auth.inherit",
             "true", strlen("true"), 0 SXA_EXTRA);
    fd0 = open((m0 / "file5").string().c_str(), O_CREAT|O_RDWR, 0644);
    BOOST_CHECK(fd0 != -1);
    BOOST_CHECK_EQUAL(write(fd0, "coin", 4), 4);
    fsync(fd0);
    BOOST_CHECK_EQUAL(stat((m0/"file5").c_str(), &st), 0);
    usleep(2100000);
    BOOST_CHECK_EQUAL(stat((m1/"file5").c_str(), &st), 0);
    bfs::remove(m1 / "file5");
    BOOST_CHECK_EQUAL(write(fd0, "coin", 4), 4);
    BOOST_CHECK_EQUAL(close(fd0), 0);
    BOOST_CHECK_EQUAL(stat((m0/"file5").c_str(), &st), -1);
    BOOST_CHECK_EQUAL(stat((m1/"file5").c_str(), &st), -1);
  }
  ELLE_LOG("write/replace")
  {
    int fd0, fd1;
    fd0 = open((m0 / "file6").string().c_str(), O_CREAT|O_RDWR, 0644);
    BOOST_CHECK(fd0 != -1);
    BOOST_CHECK_EQUAL(write(fd0, "coin", 4), 4);
    BOOST_CHECK_EQUAL(close(fd0), 0);
    fd1 = open((m1 / "file6bis").string().c_str(), O_CREAT|O_RDWR, 0644);
    BOOST_CHECK(fd1 != -1);
    BOOST_CHECK_EQUAL(write(fd1, "nioc", 4), 4);
    BOOST_CHECK_EQUAL(close(fd1), 0);
    fd0 = open((m0 / "file6").string().c_str(), O_CREAT|O_RDWR|O_APPEND, 0644);
    BOOST_CHECK(fd0 != -1);
    ELLE_LOG("write");
    BOOST_CHECK_EQUAL(write(fd0, "coin", 4), 4);
    ELLE_LOG("rename");
    bfs::rename(m1 / "file6bis", m1 / "file6");
    ELLE_LOG("close");
    BOOST_CHECK_EQUAL(close(fd0), 0);
    BOOST_CHECK_EQUAL(read(m0/"file6"), "nioc");
    BOOST_CHECK_EQUAL(read(m1/"file6"), "nioc");
  }
}

void
conflicts()
{
  test_conflicts(false);
}

void
conflicts_paxos()
{
  test_conflicts(true);
}

static
void
test_acl(bool paxos)
{
  namespace bfs = boost::filesystem;
  boost::system::error_code erc;
  auto store = bfs::temp_directory_path() / bfs::unique_path();
  auto mount = bfs::temp_directory_path() / bfs::unique_path();
  bfs::create_directories(mount);
  bfs::create_directories(store);
  struct statvfs statstart;
  statvfs(mount.string().c_str(), &statstart);
  mount_points.clear();
  std::thread t([&] {
      run_filesystem_dht(store.string(), mount.string(), 5, 1, 1, 2, paxos);
  });
  wait_for_mounts(mount, 2, &statstart);
  ELLE_LOG("Test start");
  elle::SafeFinally remover([&] {
    try
    {
      unmounter(mount, store, t);
    }
    catch (std::exception const& e)
    {
      ELLE_TRACE("unmounter threw %s", e.what());
    }
  });

  // Mounts/keys are in mount_points and keys
  // First entry got the root!
  BOOST_CHECK_EQUAL(mount_points.size(), 2);
  bfs::path m0 = mount_points[0];
  bfs::path m1 = mount_points[1];
  //bfs::path m2 = mount_points[2];
  BOOST_CHECK_EQUAL(keys.size(), 2);
  std::string k1 = serialize(keys[1]);
  {
    boost::filesystem::ofstream ofs(m0 / "test");
    ofs << "Test";
  }
  BOOST_CHECK_EQUAL(directory_count(m0), 1);
  BOOST_CHECK_EQUAL(directory_count(m1), -1);
  BOOST_CHECK(!can_access(m1/"test"));
  {
     boost::filesystem::ifstream ifs(m1 / "test");
     BOOST_CHECK_EQUAL(ifs.good(), false);
  }
  setxattr(m0.c_str(), "user.infinit.auth.setrw",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  // expire directory cache
  usleep(2100000);
  // k1 can now list directory
  BOOST_CHECK_EQUAL(directory_count(m1), 1);
  // but the file is still not readable
  BOOST_CHECK(!can_access(m1/"test"));
  boost::filesystem::remove(m1 / "test", erc);
  BOOST_CHECK(erc);
  BOOST_CHECK_EQUAL(directory_count(m1), 1);
  BOOST_CHECK_EQUAL(directory_count(m0), 1);
  setxattr((m0/"test").c_str(), "user.infinit.auth.setrw",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  usleep(2100000);
  BOOST_CHECK(can_access(m1/"test"));
  {
     boost::filesystem::ifstream ifs(m1 / "test");
     BOOST_CHECK_EQUAL(ifs.good(), true);
     std::string v;
     ifs >> v;
     BOOST_CHECK_EQUAL(v, "Test");
  }
  setxattr((m0/"test").c_str(), "user.infinit.auth.clear",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  usleep(3100000);
  BOOST_CHECK(!can_access(m1/"test"));
  setxattr((m0/"test").c_str(), "user.infinit.auth.setrw",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  usleep(2100000);
  BOOST_CHECK(can_access(m1/"test"));
  bfs::create_directory(m0 / "dir1");
  BOOST_CHECK(touch(m0 / "dir1" / "pan"));
  BOOST_CHECK(!can_access(m1 / "dir1"));
  BOOST_CHECK(!can_access(m1 / "dir1" / "pan"));
  BOOST_CHECK(!touch(m1 / "dir1" / "coin"));
  setxattr((m0 / "dir1").c_str(), "user.infinit.auth.setrw",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  usleep(2100000);
  BOOST_CHECK(can_access(m1 / "dir1"));
  BOOST_CHECK(!can_access(m1 / "dir1" / "pan"));
  BOOST_CHECK(touch(m1 / "dir1" / "coin"));
  // test by user name
  touch(m0 / "byuser");
  BOOST_CHECK(!can_access(m1 / "byuser"));
  setxattr((m0 / "byuser").c_str(), "user.infinit.auth.setrw",
    "user1", strlen("user1"), 0 SXA_EXTRA);
  usleep(2100000);
  BOOST_CHECK(can_access(m1/"test"));
  BOOST_CHECK(can_access(m1 / "byuser"));
  // inheritance
  bfs::create_directory(m0 / "dirs");
  ELLE_LOG("setattrs");
  setxattr((m0 / "dirs").c_str(), "user.infinit.auth.setrw",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  usleep(2100000);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dirs"), 0);
  ELLE_LOG("setinherit");
  setxattr((m0 / "dirs").c_str(), "user.infinit.auth.inherit",
    "true", strlen("true"), 0 SXA_EXTRA);
  ELLE_LOG("create childs");
  touch(m0 / "dirs" / "coin");
  bfs::create_directory(m0 / "dirs" / "dir");
  touch(m0 / "dirs" / "dir" / "coin");
  usleep(2100000);
  BOOST_CHECK(can_access(m1 / "dirs" / "coin"));
  BOOST_CHECK(can_access(m1 / "dirs" / "dir" / "coin"));
  BOOST_CHECK_EQUAL(directory_count(m1 / "dirs"), 2);
  // readonly
  bfs::create_directory(m0 / "dir2");
  setxattr((m0 / "dir2").c_str(), "user.infinit.auth.setr",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  BOOST_CHECK(touch(m0 / "dir2" / "coin"));
  setxattr((m0 / "dir2"/ "coin").c_str(), "user.infinit.auth.setr",
    k1.c_str(), k1.length(), 0 SXA_EXTRA);
  usleep(2100000);
  BOOST_CHECK(can_access(m1 / "dir2" / "coin"));
  BOOST_CHECK(!touch(m1 / "dir2" / "coin"));
  BOOST_CHECK(!touch(m1 / "dir2" / "pan"));

  ELLE_LOG("world-readable");
  setxattr((m0).c_str(), "user.infinit.auth.inherit",
    "false", strlen("false"), 0 SXA_EXTRA);
  bfs::create_directory(m0 / "dir3");
  bfs::create_directory(m0 / "dir3" / "dir");
  {
     boost::filesystem::ofstream ofs(m0 / "dir3" / "file");
     ofs << "foo";
  }
  BOOST_CHECK_EQUAL(directory_count(m0 / "dir3"), 2);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir3"), -1);
  // open dir3
  bfs::permissions(m0 / "dir3", bfs::add_perms | bfs::others_read);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir3"), 2);
  bfs::create_directory(m1 / "dir3" / "tdir", erc);
  BOOST_CHECK(erc);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir3" / "dir"), -1);
  // close dir3
  bfs::permissions(m0 / "dir3", bfs::remove_perms | bfs::others_read);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir3"), -1);
  bfs::permissions(m0 / "dir3", bfs::add_perms | bfs::others_read);
  bfs::permissions(m0 / "dir3" / "file", bfs::add_perms | bfs::others_read);
  bfs::permissions(m0 / "dir3" / "dir", bfs::add_perms | bfs::others_read);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir3" / "dir"), 0);
  BOOST_CHECK_EQUAL(read(m1 / "dir3" / "file"), "foo");
  write(m1 / "dir3" / "file", "babar");
  BOOST_CHECK_EQUAL(read(m1 / "dir3" / "file"), "foo");
  BOOST_CHECK_EQUAL(read(m0 / "dir3" / "file"), "foo");
  write(m0 / "dir3" / "file", "bim");
  BOOST_CHECK_EQUAL(read(m1 / "dir3" / "file"), "bim");
  BOOST_CHECK_EQUAL(read(m0 / "dir3" / "file"), "bim");
  bfs::create_directory(m0 / "dir3" / "dir2");
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir3"), 3);
  BOOST_CHECK_EQUAL(directory_count(m0 / "dir3"), 3);
  bfs::permissions(m0 / "dir3" / "file", bfs::remove_perms | bfs::others_read);
  write(m0 / "dir3" / "file", "foo2");
  BOOST_CHECK_EQUAL(read(m0 / "dir3" / "file"), "foo2");

  ELLE_LOG("world-writable");
  bfs::create_directory(m0 / "dir4");
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir4"), -1);
  bfs::permissions(m0 / "dir4", bfs::add_perms |bfs::others_write | bfs::others_read);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir4"), 0);
  write(m1 / "dir4" / "file", "foo");
  bfs::create_directory(m1 /"dir4"/ "dir");
  BOOST_CHECK_EQUAL(read(m0 / "dir4" / "file"), "");
  BOOST_CHECK_EQUAL(read(m1 / "dir4" / "file"), "foo");
  BOOST_CHECK_EQUAL(directory_count(m0 / "dir4" / "dir"), -1);
  BOOST_CHECK_EQUAL(directory_count(m1 / "dir4" / "dir"), 0);
  bfs::permissions(m0 / "dir4", bfs::remove_perms |bfs::others_write);
  BOOST_CHECK_EQUAL(read(m1 / "dir4" / "file"), "foo");

  write(m0 / "file5", "foo");
  bfs::permissions(m0 / "file5", bfs::add_perms |bfs::others_write | bfs::others_read);
  write(m1 / "file5", "bar");
  BOOST_CHECK_EQUAL(read(m1 / "file5"), "bar");
  BOOST_CHECK_EQUAL(read(m0 / "file5"), "bar");
  bfs::permissions(m0 / "file5", bfs::remove_perms |bfs::others_write);
  write(m1 / "file5", "barbar");
  BOOST_CHECK_EQUAL(read(m1 / "file5"), "bar");
  BOOST_CHECK_EQUAL(read(m0 / "file5"), "bar");

  ELLE_LOG("groups");
  write(m0 / "g1", "foo\n");
  BOOST_CHECK_EQUAL(read(m0 / "g1"), "foo");
  BOOST_CHECK_EQUAL(read(m1 / "g1"), "");
  group_create(m0, "group1");
  group_add(m0, "group1", "user1");
  setxattr((m0 / "g1").c_str(), "user.infinit.auth.setrw",
    "@group1", 7, 0 SXA_EXTRA);
  group_load(m1, "group1");
  usleep(100000);
  BOOST_CHECK_EQUAL(read(m1 / "g1"), "foo");
  group_remove(m0, "group1", "user1");
  write(m0 / "g2", "foo");
  setxattr((m0 / "g2").c_str(), "user.infinit.auth.setrw",
    "@group1", 7, 0 SXA_EXTRA);
  BOOST_CHECK_EQUAL(read(m1 / "g2"), "");
  group_add(m0, "group1", "user1");
  group_load(m1, "group1");
  usleep(100000);
  BOOST_CHECK_EQUAL(read(m1 / "g1"), "foo");
  //incorrect stuff, check it doesn't crash us
  group_create(m0, "group1");
  group_add(m0, "group1","user1");
  group_add(m0, "group1","user1");
  group_remove(m0, "group1", "user1");
  group_remove(m0, "group1", "user1");
  group_add(m0, "group1", "group1");
  group_add(m0, "nosuch", "user1");
  group_add(m0, "group1","nosuch");
  group_remove(m0, "group1","nosuch");
  group_load(m0, "group1");
  group_load(m0, "nosuch");
  BOOST_CHECK_EQUAL(read(m0 / "g1"), "foo");
  ELLE_LOG("test end");
}

static
void
acl()
{
  test_acl(false);
}

static
void
acl_paxos()
{
  test_acl(true);
}

ELLE_TEST_SUITE()
{
  // This is needed to ignore child process exiting with nonzero
  // There is unfortunately no more specific way.
  setenv("BOOST_TEST_CATCH_SYSTEM_ERRORS", "no", 1);
  signal(SIGCHLD, SIG_IGN);
  auto& suite = boost::unit_test::framework::master_test_suite();
  // only doughnut supported filesystem->add(BOOST_TEST_CASE(test_basic), 0, 50);
  suite.add(BOOST_TEST_CASE(filesystem), 0, 120);
  suite.add(BOOST_TEST_CASE(filesystem_paxos), 0, 120);
#ifndef INFINIT_MACOSX
  // osxfuse fails to handle two mounts at the same time, the second fails
  // with a mysterious 'permission denied'
  suite.add(BOOST_TEST_CASE(acl), 0, 120);
  suite.add(BOOST_TEST_CASE(acl_paxos), 0, 120);
  suite.add(BOOST_TEST_CASE(conflicts), 0, 120);
  suite.add(BOOST_TEST_CASE(conflicts_paxos), 0, 120);
#endif
}
