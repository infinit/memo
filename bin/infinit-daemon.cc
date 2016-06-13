#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#ifdef INFINIT_MACOSX
#  include <sys/param.h>
#  include <sys/mount.h>
#endif

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/system/PIDFile.hh>
#include <elle/system/Process.hh>
#include <elle/serialization/json.hh>
#include <elle/system/self-path.hh>

#include <reactor/network/http-server.hh>
#include <reactor/network/unix-domain-server.hh>
#include <reactor/network/unix-domain-socket.hh>

#include <infinit/utility.hh>

#include <infinit/storage/Filesystem.hh>

ELLE_LOG_COMPONENT("infinit-daemon");

#include <main.hh>
#include <password.hh>

infinit::Infinit ifnt;

void link_network(std::string const& name)
{
  auto desc = ifnt.network_descriptor_get(name, {}, false);
  auto users = ifnt.users_get();
  boost::optional<infinit::Passport> passport;
  boost::optional<infinit::User> user;
  ELLE_TRACE("checking if any user is owner");
  for (auto const& u: users)
  {
    if (u.public_key == desc.owner)
    {
      passport.emplace(u.public_key, desc.name,
        infinit::cryptography::rsa::KeyPair(u.public_key,
                                            u.private_key.get()));
      user.emplace(u);
      break;
    }
  }
  if (!passport)
  {
    ELLE_TRACE("Trying to acquire passport");
    for (auto const& u: users)
    {
      try
      {
        passport.emplace(ifnt.passport_get(name, u.name));
        user.emplace(u);
        break;
      }
      catch (MissingLocalResource const&)
      {
        try
        {
          passport.emplace(beyond_fetch<infinit::Passport>(elle::sprintf(
            "networks/%s/passports/%s", name, u.name),
              "passport for",
              name,
              u));
          user.emplace(u);
          break;
        }
        catch (elle::Error const&)
        {}
      }
    }
  }
  if (!passport)
    throw elle::Error("Failed to acquire passport.");
  ELLE_TRACE("Passport found for user %s", user->name);
  infinit::Network network(
    desc.name,
    elle::make_unique<infinit::model::doughnut::Configuration>(
      infinit::model::Address::random(0), // FIXME
      std::move(desc.consensus),
      std::move(desc.overlay),
      std::unique_ptr<infinit::storage::StorageConfig>(),
      user->keypair(),
      std::make_shared<infinit::cryptography::rsa::PublicKey>(desc.owner),
      std::move(*passport),
      user->name,
      boost::optional<int>(),
      desc.version,
      desc.admin_keys));
  ifnt.network_save(network, true);
}

void acquire_network(std::string const& name)
{
  infinit::NetworkDescriptor desc = beyond_fetch<infinit::NetworkDescriptor>("network", name);
  ifnt.network_save(desc);
  try
  {
    auto net = ifnt.network_get(name, {}, true);
  }
  catch (elle::Error const&)
  {
    link_network(name);
  }
}

void acquire_volume(std::string const& name)
{
  auto desc = beyond_fetch<infinit::Volume>("volume", name);
  ifnt.volume_save(desc, true);
  try
  {
    auto net = ifnt.network_get(desc.network, {}, true);
  }
  catch (MissingLocalResource const&)
  {
    acquire_network(desc.network);
  }
  catch (elle::Error const&)
  {
    link_network(desc.network);
  }
}

void acquire_volumes()
{
  auto users = ifnt.users_get();
  for (auto const& u: users)
  {
    auto res = beyond_fetch<
      std::unordered_map<std::string, std::vector<infinit::Volume>>>(
        elle::sprintf("users/%s/volumes", u.name),
        "volumes for user",
        u.name,
        u);
    for (auto const& volume: res["volumes"])
    {
      try
      {
        acquire_volume(volume.name);
      }
      catch (ResourceAlreadyFetched const& error)
      {
      }
      catch (elle::Error const& e)
      {
        ELLE_LOG("failed to acquire %s: %s", volume.name, e);
      }
    }
  }
}


struct Mount
{
  std::unique_ptr<elle::system::Process> process;
  infinit::MountOptions options;
};

class MountManager
{
public:
  MountManager(boost::filesystem::path mount_root = boost::filesystem::temp_directory_path(),
               std::string mount_substitute = "")
   : _mount_root(mount_root)
   , _mount_substitute(mount_substitute)
   {}
  void
  start(std::string const& name, infinit::MountOptions opts = {},
        bool force_mount = false,
        bool wait_for_mount = false);
  void
  stop(std::string const& name);
  void
  status(std::string const& name, elle::serialization::SerializerOut& reply);
  bool
  exists(std::string const& name);
  std::string
  mountpoint(std::string const& name);
  std::vector<std::string>
  list();
  void
  create_volume(std::string const& name,
                elle::json::Object const& args);
  infinit::Network
  create_network(elle::json::Object const& options,
                 infinit::User const& owner);
  void
  update_network(infinit::Network& network,
                 elle::json::Object const& options);
  ELLE_ATTRIBUTE_RW(boost::filesystem::path, mount_root);
  ELLE_ATTRIBUTE_RW(std::string, mount_substitute);
  ELLE_ATTRIBUTE_RW(boost::optional<std::string>, log_level);
  ELLE_ATTRIBUTE_RW(boost::optional<std::string>, log_path);
  ELLE_ATTRIBUTE_RW(std::string, default_user);
  ELLE_ATTRIBUTE_RW(std::string, default_network);
  ELLE_ATTRIBUTE_RW(std::vector<std::string>, advertise_host);
private:
  std::unordered_map<std::string, Mount> _mounts;
};

std::vector<std::string>
MountManager::list()
{
  try
  {
    acquire_volumes();
  }
  catch (elle::Error const& e)
  {
    ELLE_TRACE("Failed to acquire volumes from beyond: %s", e);
  }
  std::vector<std::string> res;
  for (auto const& volume: ifnt.volumes_get())
    res.push_back(volume.name);
  return res;
}

std::string
MountManager::mountpoint(std::string const& name)
{
  auto it = _mounts.find(name);
  if (it == _mounts.end())
    throw elle::Error("not mounted: " + name);
  ELLE_ASSERT(it->second.options.mountpoint);
  std::string pre = it->second.options.mountpoint.get();
  if (!this->_mount_substitute.empty())
  {
    auto sep = this->_mount_substitute.find(":");
    if (sep == std::string::npos)
      pre = this->_mount_substitute + pre;
    else
    {
      std::string search = this->_mount_substitute.substr(0, sep);
      std::string repl = this->_mount_substitute.substr(sep+1);
      auto pos = pre.find(search);
      if (pos != pre.npos)
      {
        pre = pre.substr(0, pos) + repl + pre.substr(pos + search.size());
      }
    }
  }
  ELLE_TRACE("replying with mountpoint %s", pre);
  return pre;
}

bool
MountManager::exists(std::string const& name_)
{
  std::string name(name_);
  if (name.find("/") == name.npos)
    name = elle::system::username() + "/" + name;
  try
  {
    auto volume = ifnt.volume_get(name);
    return true;
  }
  catch (elle::Error const& e)
  {
    return false;
  }
}

bool
is_mounted(std::string const& path)
{
#ifdef INFINIT_LINUX
  auto mounts = boost::filesystem::path("/proc") / std::to_string(getpid()) / "mounts";
  boost::filesystem::ifstream ifs(mounts);
  while (!ifs.eof())
  {
    std::string line;
    std::getline(ifs, line);
    std::vector<std::string> elems;
    boost::algorithm::split(elems, line, boost::is_any_of(" "));
    if (elems.size() >= 2 && elems.at(1) == path)
      return true;
  }
  return false;
#elif defined(INFINIT_WINDOWS)
  // We mount as drive letters under windows
  return boost::filesystem::exists(path);
#elif defined(INFINIT_MACOSX)
  struct statfs sfs;
  int res = statfs(path.c_str(), &sfs);
  if (res)
    return false;
  return boost::filesystem::path(path) == boost::filesystem::path(sfs.f_mntonname);
#else
  throw elle::Error("is_mounted is not implemented");
#endif
}

void
MountManager::start(std::string const& name, infinit::MountOptions opts,
                    bool force_mount,
                    bool wait_for_mount)
{
  infinit::Volume volume;
  try
  {
    volume = ifnt.volume_get(name);
  }
  catch (MissingLocalResource const&)
  {
    acquire_volume(name);
    volume = ifnt.volume_get(name);
  }
  volume.mount_options.merge(opts);
  Mount m{nullptr, volume.mount_options};
  if (force_mount && !m.options.mountpoint)
    m.options.mountpoint =
    (this->_mount_root / boost::filesystem::unique_path()).string();
  std::vector<std::string> arguments;
  static const auto root = elle::system::self_path().parent_path();
  arguments.push_back((root / "infinit-volume").string());
  arguments.push_back("--run");
  arguments.push_back(volume.name);
  std::unordered_map<std::string, std::string> env;
  m.options.to_commandline(arguments, env);
  for (auto const& host: _advertise_host)
  {
    arguments.push_back("--advertise-host");
    arguments.push_back(host);
  }
  if (this->_log_level)
    env.insert(std::make_pair("ELLE_LOG_LEVEL", _log_level.get()));
  if (this->_log_path)
  {
    std::string sname(name);
    boost::replace_all(sname, "/", "_");
    env.insert(
      std::make_pair("ELLE_LOG_FILE",
                     _log_path.get() + "/infinit-volume-" + sname
                     + '-' + boost::posix_time::to_iso_extended_string(
                       boost::posix_time::microsec_clock::universal_time())
                     + ".log"));
  }
  ELLE_TRACE("Spawning with %s %s", arguments, env);
  // FIXME upgrade Process to accept env
  for (auto const& e: env)
    elle::os::setenv(e.first, e.second, true);
  m.process = elle::make_unique<elle::system::Process>(arguments);
  int pid = m.process->pid();
  std::thread t([pid] {
      int status = 0;
      ::waitpid(pid, &status, 0);
  });
  t.detach();
  auto mountpoint = m.options.mountpoint;
  this->_mounts.emplace(name, std::move(m));
  if (wait_for_mount && mountpoint)
  {
    for (int i=0; i<100; ++i)
    {
      if (kill(pid, 0))
      {
        ELLE_TRACE("Process is dead: %s", strerror(errno));
        break;
      }
      if (is_mounted(mountpoint.get()))
        return;
      reactor::sleep(100_ms);
    }
    ELLE_ERR("mount of %s failed", name);
    stop(name);
    throw elle::Error("Mount failure for " + name);
  }
}

void
MountManager::stop(std::string const& name)
{
  auto it = _mounts.find(name);
  if (it == _mounts.end())
    throw elle::Error("not mounted: " + name);
  ::kill(it->second.process->pid(), SIGTERM); // FIXME: try harder
  this->_mounts.erase(it);
}

void
MountManager::status(std::string const& name,
                     elle::serialization::SerializerOut& reply)
{
  auto it = this->_mounts.find(name);
  if (it == this->_mounts.end())
    throw elle::Error("not mounted: " + name);
  bool live = ! kill(it->second.process->pid(), 0);
  reply.serialize("live", live);
  if (it->second.options.mountpoint)
  {
    reply.serialize("mountpoint", mountpoint(name));
  }
}

static
boost::optional<std::string>
optional(elle::json::Object const& options, std::string const& name)
{
  auto it = options.find(name);
  if (it == options.end())
    return {};
  else
    return boost::any_cast<std::string>(it->second);
}

void
MountManager::update_network(infinit::Network& network,
                             elle::json::Object const& options)
{
  bool updated = false;
  auto storagedesc = optional(options, "storage");
  if (storagedesc)
  {
    updated = true;
    std::unique_ptr<infinit::storage::StorageConfig> storage_config;
    if (storagedesc->empty())
    {
      std::string storagename = network.name + "_storage";
      boost::replace_all(storagename, "/", "_");
      ELLE_LOG("Creating local storage %s", storagename);
      auto path = infinit::xdg_data_home() / "blocks" / storagename;
      storage_config = elle::make_unique<infinit::storage::FilesystemStorageConfig>(
        storagename, path.string(), boost::optional<int64_t>());
    }
    else
    {
      try
      {
        storage_config = ifnt.storage_get(*storagedesc);
      }
      catch (MissingLocalResource const&)
      {
        throw elle::Error("Storage specification for new storage not implemented");
      }
    }
    network.model->storage = std::move(storage_config);
  }
  auto portstring = optional(options, "port");
  if (portstring)
  {
    auto dht = dynamic_cast<infinit::model::doughnut::Configuration*>(network.model.get());
    dht->port = std::stoi(*portstring);
    if (*dht->port == 0)
      dht->port.reset();
    updated = true;
  }
  if (updated)
  {
    ifnt.network_save(network, true);
  }
}

infinit::Network
MountManager::create_network(elle::json::Object const& options,
                             infinit::User const& owner)
{
  auto netname = optional(options, "network");
  if (!netname)
    netname = _default_network;
  ELLE_LOG("Creating network %s", netname);
  std::unique_ptr<infinit::storage::StorageConfig> storage_config;
  auto storagedesc = optional(options, "storage");
  if (!storagedesc || storagedesc->empty())
  {
    std::string storagename = *netname + "_storage";
    boost::replace_all(storagename, "/", "_");
    ELLE_LOG("Creating local storage %s", storagename);
    auto path = infinit::xdg_data_home() / "blocks" / storagename;
    storage_config = elle::make_unique<infinit::storage::FilesystemStorageConfig>(
      storagename, path.string(), boost::optional<int64_t>());
  }
  else
  {
    try
    {
      storage_config = ifnt.storage_get(*storagedesc);
    }
    catch (MissingLocalResource const&)
    {
      throw elle::Error("Storage specification for new storage not implemented");
    }
  }
  // create the network
   auto kelips =
    elle::make_unique<infinit::overlay::kelips::Configuration>();
   kelips->k = 1;
   kelips->rpc_protocol = infinit::model::doughnut::Local::Protocol::all;
   std::unique_ptr<infinit::model::doughnut::consensus::Configuration> consensus_config;
   consensus_config = elle::make_unique<
      infinit::model::doughnut::consensus::Paxos::Configuration>(
        1, // replication_factor,
        std::chrono::seconds(10 * 60));
  boost::optional<int> port;
  auto portstring = optional(options, "port");
  if (portstring)
    port = std::stoi(*portstring);
  auto dht =
    elle::make_unique<infinit::model::doughnut::Configuration>(
      infinit::model::Address::random(0), // FIXME
      std::move(consensus_config),
      std::move(kelips),
      std::move(storage_config),
      owner.keypair(),
      std::make_shared<infinit::cryptography::rsa::PublicKey>(owner.public_key),
      infinit::model::doughnut::Passport(
        owner.public_key,
        ifnt.qualified_name(*netname, owner),
        infinit::cryptography::rsa::KeyPair(owner.public_key,
                                            owner.private_key.get())),
      owner.name,
      port,
      version,
      infinit::model::doughnut::AdminKeys());
  infinit::Network network(ifnt.qualified_name(*netname, owner),
                           std::move(dht));
  ifnt.network_save(network);
  report_created("network", *netname);
  infinit::NetworkDescriptor desc(ifnt.network_get(*netname, owner, true));
  beyond_push("network", desc.name, desc, owner);
  return network;
}

void
MountManager::create_volume(std::string const& name,
                            elle::json::Object const& options)
{
  try
  {
    acquire_volumes();
  }
  catch (elle::Error const& e)
  {
    ELLE_TRACE("Failed to acquire volumes from beyond: %s", e);
  }
  auto username = optional(options, "user");
  if (!username)
    username = _default_user;
  auto user = ifnt.user_get(*username);
  auto netname = optional(options, "network");
  if (!netname)
    netname = _default_network;
  infinit::Network network ([&]() -> infinit::Network {
      try
      {
        auto net = ifnt.network_get(*netname, user, true);
        update_network(net, options);
        return net;
      }
      catch (MissingLocalResource const&)
      {
        return create_network(options, user);
      }
  }());
  infinit::MountOptions mo;
  mo.fetch = true;
  mo.push = true;
  mo.as = username;
  mo.cache = !optional(options, "nocache");;
  std::string qname(name);
  if (qname.find("/") == qname.npos)
    qname = *username + "/" + qname;
  infinit::Volume volume(qname, network.name, mo, {});
  ifnt.volume_save(volume);
  report_created("volume", qname);
  beyond_push("volume", qname, volume, user);
}

class DockerVolumePlugin
{
public:
  DockerVolumePlugin(MountManager& manager);
  ~DockerVolumePlugin();
  void install(bool tcp, int tcp_port,
               boost::filesystem::path socket_path,
               boost::filesystem::path descriptor_path);
  void uninstall();
private:
  MountManager& _manager;
  std::unique_ptr<reactor::network::HttpServer> _server;
  std::unordered_map<std::string, int> _mount_count;
};

static
std::string
daemon_command(std::string const& s);

class PIDFile
  : public elle::PIDFile
{
public:
  PIDFile()
    : elle::PIDFile(this->path())
  {}

  static
  boost::filesystem::path
  path()
  {
    return infinit::xdg_runtime_dir () / "daemon.pid";
  }

  static
  int
  read()
  {
    return elle::PIDFile::read(PIDFile::path());
  }
};

static
std::string
daemon_command(std::string const& s);

static
int
daemon_running()
{
  int pid = -1;
  try
  {
    pid = PIDFile::read();
  }
  catch (elle::Error const& e)
  {
    ELLE_TRACE("error getting PID: %s", e);
    return 0;
  }
  if (kill(pid, 0) != 0)
    return 0;
  try
  {
    daemon_command("{\"operation\": \"status\"}");
    return pid;
  }
  catch (elle::Error const& e)
  {
    ELLE_TRACE("status command threw %s", e);
    return 0;
  }
}

static
void
daemon_stop()
{
  int pid = daemon_running();
  if (!pid)
    elle::err("daemon is not running");
  try
  {
    daemon_command("{\"operation\": \"stop\"}");
  }
  catch (elle::Error const& e)
  {
    ELLE_TRACE("stop command threw %s", e);
  }
  for (int i = 0; i<50; ++i)
  {
    if (kill(pid, 0))
    {
      std::cout << "daemon stopped" << std::endl;
      return;
    }
    usleep(100000);
  }
  ELLE_TRACE("Sending TERM to %s", pid);
  if (kill(pid, SIGTERM))
    ELLE_TRACE("kill failed");
  for (int i=0; i<50; ++i)
  {
    if (kill(pid, 0))
      return;
    usleep(100000);
  }
  ELLE_TRACE("Process still running, sending KILL");
  kill(pid, SIGKILL);
  for (int i=0; i<50; ++i)
  {
    if (kill(pid, 0))
      return;
    usleep(100000);
  }
}

static
void
daemonize()
{
  if (daemon(1, 0))
    elle::err("failed to daemonize: %s", strerror(errno));
}

static
std::string
daemon_command(std::string const& s)
{
  reactor::Scheduler sched;
  std::string reply;
  reactor::Thread main_thread(
    sched,
    "main",
    [&]
    {
      reactor::network::UnixDomainSocket sock(daemon_sock_path());
      std::string cmd = s + "\n";
      ELLE_TRACE("writing query: %s", s);
      sock.write(elle::ConstWeakBuffer(cmd.data(), cmd.size()));
      ELLE_TRACE("reading result");
      reply = sock.read_until("\n").string();
      ELLE_TRACE("ok: '%s'", reply);
    });
  sched.run();
  return reply;
}

static
std::string
process_command(elle::json::Object query, MountManager& manager)
{
  ELLE_TRACE("command: %s", elle::json::pretty_print(query));
  elle::serialization::json::SerializerIn command(query, false);
  std::stringstream ss;
  {
    elle::serialization::json::SerializerOut response(ss, false);
    auto op = command.deserialize<std::string>("operation");
    response.serialize("operation", op);
    try
    {
      if (op == "status")
      {
        response.serialize("status", "Ok");
      }
      else if (op == "stop")
      {
        throw elle::Exit(0);
      }
      else if (op == "volume-start")
      {
        auto volume = command.deserialize<std::string>("volume");
        auto opts =
          command.deserialize<boost::optional<infinit::MountOptions>>("options");
        manager.start(volume, opts ? opts.get() : infinit::MountOptions(),
                      false, true);
      }
      else if (op == "volume-stop")
      {
        auto volume = command.deserialize<std::string>("volume");
        manager.stop(volume);
      }
      else if (op == "volume-status")
      {
        auto volume = command.deserialize<std::string>("volume");
        manager.status(volume, response);
      }
      else
      {
        throw std::runtime_error(("unknown operation: " + op).c_str());
      }
      response.serialize("result", "Ok");
    }
    catch (elle::Exception const& e)
    {
      response.serialize("result", "Error");
      response.serialize("error", e.what());
    }
  }
  ss << '\n';
  return ss.str();
}

COMMAND(stop)
{
  daemon_stop();
}

COMMAND(status)
{
  if (daemon_running())
    std::cout << "Running" << std::endl;
  else
    std::cout << "Stopped" << std::endl;
}

COMMAND(fetch)
{
  acquire_volume(mandatory(args, "name"));
}

template<typename T = std::string>
T
with_default(boost::program_options::variables_map const& vm,
              std::string const& name,
              T default_value)
{
  auto opt = optional<T>(vm, name);
  if (!opt)
    return default_value;
  else
    return *opt;
}
COMMAND(start)
{
  ELLE_TRACE("starting daemon");
  if (daemon_running())
    elle::err("daemon already running");
  auto users = optional<std::vector<std::string>>(args, "login-user");
  if(users) for (auto const& u: *users)
  {
    auto sep = u.find_first_of(":");
    std::string name = u.substr(0, sep);
    std::string pass = u.substr(sep+1);
    LoginCredentials c {name, hash_password(pass, _hub_salt)};
    das::Serializer<DasLoginCredentials> credentials{c};
    auto json = beyond_login(name, credentials);
    elle::serialization::json::SerializerIn input(json, false);
    auto user = input.deserialize<infinit::User>();
    ifnt.user_save(user, true);
    report_action("saved", "user", name, std::string("locally"));
  }
  ELLE_TRACE("starting manager");
  MountManager manager(
    with_default<std::string>(args, "mount-root", boost::filesystem::temp_directory_path().string()),
    with_default<std::string>(args, "docker-mount-substitute", ""));
  DockerVolumePlugin dvp(manager);
  dvp.install(flag(args, "docker-socket-tcp"),
              std::stoi(with_default<std::string>(args, "docker-socket-port", "0")),
              with_default<std::string>(args, "docker-socket-path", "/run/docker/plugins"),
              with_default<std::string>(args, "docker-descriptor-path", "/usr/lib/docker/plugins"));
  if (!flag(args, "foreground"))
    daemonize();
  PIDFile pid;
  reactor::network::UnixDomainServer srv;
  auto sockaddr = daemon_sock_path();
  boost::filesystem::remove(sockaddr);
  srv.listen(sockaddr);
  auto loglevel = optional(args, "log-level");
  manager.log_level(loglevel);
  auto logpath = optional(args, "log-path");
  manager.log_path(logpath);
  auto default_user = optional(args, "default-user");
  if (default_user)
    manager.default_user(*default_user);
  auto default_network = optional(args, "default-network");
  if (default_network)
    manager.default_network(*default_network);
  auto advertise = optional<std::vector<std::string>>(args, "advertise-host");
  if (advertise)
    manager.advertise_host(*advertise);
  elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
  {
    while (true)
    {
      auto socket = elle::utility::move_on_copy(srv.accept());
      auto name = elle::sprintf("%s server", **socket);
      scope.run_background(
        name,
        [socket,&manager]
        {
          try
          {
            while (true)
            {
              auto json =
                boost::any_cast<elle::json::Object>(elle::json::read(**socket));
              auto reply = process_command(json, manager);
              ELLE_TRACE("Writing reply: '%s'", reply);
              socket->write(reply);
            }
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("%s", e);
            try
            {
              socket->write(std::string("{\"error\": \"") + e.what() + "\"}\n");
            }
            catch (elle::Error const&)
            {}
          }
        });
    }
  };
}

DockerVolumePlugin::DockerVolumePlugin(MountManager& manager)
: _manager(manager)
{
}
DockerVolumePlugin::~DockerVolumePlugin()
{
  uninstall();
}

void
DockerVolumePlugin::uninstall()
{
}

void
DockerVolumePlugin::install(bool tcp,
                            int tcp_port,
                            boost::filesystem::path socket_path,
                            boost::filesystem::path descriptor_path)
{
  // plugin path is either in /etc/docker/plugins or /usr/lib/docker/plugins
  boost::system::error_code erc;
  boost::filesystem::create_directories(descriptor_path, erc);
  if (tcp)
  {
    auto unix_socket_path = socket_path / "infinit.sock";
    boost::filesystem::remove(unix_socket_path, erc);
    this->_server = elle::make_unique<reactor::network::HttpServer>(tcp_port);
    int port = _server->port();
    std::string url = "tcp://localhost:" + std::to_string(port);
    boost::filesystem::ofstream ofs(descriptor_path / "infinit.spec");
    if (!ofs.good())
    {
      ELLE_LOG("Execute the following command: echo %s |sudo tee %s/infinit.spec",
               url, descriptor_path.string());
    }
    ofs << url;
  }
  else
  {
    boost::filesystem::remove(descriptor_path / "infinit.spec", erc);
    auto us = elle::make_unique<reactor::network::UnixDomainServer>();
    auto sock_path = socket_path / "infinit.sock";
    boost::filesystem::create_directories(sock_path.parent_path());
    boost::filesystem::remove(sock_path, erc);
    us->listen(sock_path);
    this->_server = elle::make_unique<reactor::network::HttpServer>(std::move(us));
  }
  {
    auto json = "\"name\": \"infinit\", \"address\": \"http://www.infinit.sh\"";
    boost::filesystem::ofstream ofs(descriptor_path / "infinit.json");
    if (!ofs.good())
    {
      ELLE_LOG("Execute the following command: echo '%s' |sudo tee %s/infinit.json",
               json, descriptor_path.string());
    }
    ofs << json;
  }
  #define ROUTE_SIG  (reactor::network::HttpServer::Headers const&,     \
                      reactor::network::HttpServer::Cookies const&,     \
                      reactor::network::HttpServer::Parameters const&,  \
                      elle::Buffer const& data) -> std::string
  _server->register_route("/Plugin.Activate",  reactor::http::Method::POST,
    [] ROUTE_SIG {
      ELLE_TRACE("Activating plugin");
      return "{\"Implements\": [\"VolumeDriver\"]}";
    });
  _server->register_route("/VolumeDriver.Create", reactor::http::Method::POST,
    [this] ROUTE_SIG {
      auto stream = elle::IOStream(data.istreambuf());
      auto json = boost::any_cast<elle::json::Object>(elle::json::read(stream));
      std::cerr << elle::json::pretty_print(json) << std::endl;
      std::string err;
      try
      {
        elle::json::Object opts;
        try
        {
          opts = boost::any_cast<elle::json::Object>(json.at("Opts"));
        }
        catch(...)
        {}
        auto name = optional(json, "Name");
        if (!name)
          throw elle::Error("Missing 'Name' argument");
        // Hack to force docker to invoke our Create method on existing volume,
        // which we want to do to update configuration
        auto p = name->find('@');
        if (p != std::string::npos)
          name = name->substr(0, p);
        _manager.create_volume(*name, opts);
      }
      catch (ResourceAlreadyFetched const&)
      { // this can happen, docker seems to be caching volume list:
        // a mount request can trigger a create request without any list
      }
      catch (elle::Error const& e)
      {
        err = elle::sprintf("%s", e);
        ELLE_LOG("%s\n%s", e, e.backtrace());
      }
      boost::replace_all(err, "\"", "'");
      // Since we fetch on demand, we must let create pass
      return "{\"Err\": \"" + err + "\"}";
      //return "{\"Err\": \"Use 'infinit-volume --create'\"}";
    });
  _server->register_route("/VolumeDriver.Remove", reactor::http::Method::POST,
    [] ROUTE_SIG {
      return "{\"Err\": \"Use 'infinit-volume --delete'\"}";
    });
  _server->register_route("/VolumeDriver.Get", reactor::http::Method::POST,
    [this] ROUTE_SIG {
      auto stream = elle::IOStream(data.istreambuf());
      auto json = boost::any_cast<elle::json::Object>(elle::json::read(stream));
      auto name = boost::any_cast<std::string>(json.at("Name"));
      if (_manager.exists(name))
        return "{\"Err\": \"\", \"Volume\": {\"Name\": \"" + name + "\" }}";
      else
        return "{\"Err\": \"No such mount\"}";
    });
  _server->register_route("/VolumeDriver.Mount", reactor::http::Method::POST,
    [this] ROUTE_SIG {
      auto stream = elle::IOStream(data.istreambuf());
      auto json = boost::any_cast<elle::json::Object>(elle::json::read(stream));
      auto name = boost::any_cast<std::string>(json.at("Name"));
      auto it = _mount_count.find(name);
      if (it != _mount_count.end())
      {
        ELLE_TRACE("Already mounted");
        ++it->second;
      }
      else
      {
        _manager.start(name, {}, true, true);
        _mount_count.insert(std::make_pair(name, 1));
      }
      std::string res = "{\"Err\": \"\", \"Mountpoint\": \""
          + _manager.mountpoint(name) +"\"}";
      ELLE_TRACE("reply: %s", res);
      return res;
    });
  _server->register_route("/VolumeDriver.Unmount", reactor::http::Method::POST,
    [this] ROUTE_SIG {
      auto stream = elle::IOStream(data.istreambuf());
      auto json = boost::any_cast<elle::json::Object>(elle::json::read(stream));
      auto name = boost::any_cast<std::string>(json.at("Name"));
      auto it = _mount_count.find(name);
      if (it == _mount_count.end())
        return "{\"Err\": \"No such mount\"}";
      --it->second;
      if (it->second == 0)
      {
        _mount_count.erase(it);
        _manager.stop(name);
      }
      return "{\"Err\": \"\"}";
    });
  _server->register_route("/VolumeDriver.Path", reactor::http::Method::POST,
    [this] ROUTE_SIG {
      auto stream = elle::IOStream(data.istreambuf());
      auto json = boost::any_cast<elle::json::Object>(elle::json::read(stream));
      auto name = boost::any_cast<std::string>(json.at("Name"));
      try
      {
        return "{\"Err\": \"\", \"Mountpoint\": \""
          + _manager.mountpoint(name) +"\"}";
      }
      catch (elle::Error const& e)
      {
        std::string err = elle::sprintf("%s", e);
        boost::replace_all(err, "\"", "'");
        return "{\"Err\": \"" + err + "\"}";
      }
    });
  _server->register_route("/VolumeDriver.List", reactor::http::Method::POST,
    [this] ROUTE_SIG {
      auto list = _manager.list();
      std::string res("{\"Err\": \"\", \"Volumes\": [ ");
      for (auto const& n: list)
        res += "{\"Name\": \"" + n + "\"},";
      res = res.substr(0, res.size()-1);
      res += "]}";
      return res;
    });
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "fetch",
      elle::sprintf("Fetch volume and its dependencies from %s", beyond(true)),
      &fetch,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume name" },
      },
    },
    {
      "status",
      "Query daemon status",
      &status,
      "",
      {},
    },
    {
      "start",
      "Start daemon",
      &start,
      "",
      {
        { "foreground,f", bool_switch(), "Do not daemonize" },
        { "log-level,l", value<std::string>(),
          "Log level to start volumes with (default: LOG)" },
        { "log-path,d", value<std::string>(),
          "Store volume logs in given path" },
        { "docker-socket-port", value<std::string>(),
          "TCP port to use to communicate with Docker" },
        { "docker-socket-tcp", bool_switch(),
          "Use a TCP socket for docker plugin" },
        { "docker-socket-path", value<std::string>(),
          "Path for plugin socket\n(default: /run/docker/plugins)" },
        { "docker-descriptor-path", value<std::string>(),
          "Path to add plugin descriptor\n(default: /usr/lib/docker/plugins)" },
        { "mount-root", value<std::string>(),
          "Default root path for all mounts\n(default: /tmp)" },
        { "docker-mount-substitute", value<std::string>(),
          "[from:to|prefix] : Substitute 'from' to 'to' in advertised path" },
        { "default-user", value<std::string>(),
          "Default user for volume creation" },
        { "default-network", value<std::string>(),
          "Default network for volume creation" },
        { "login-user", value<std::vector<std::string>>()->multitoken(),
          "Login with selected user(s), of form 'user:password'" },
        { "advertise-host", value<std::vector<std::string>>()->multitoken(),
          "Advertise given hostname as an extra address" },
      },
    },
    {
      "stop",
      "Stop daemon",
      &stop,
      "",
      {},
    },
  };
  return infinit::main("Infinit daemon", modes, argc, argv);
}
