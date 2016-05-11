#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/system/Process.hh>
#include <elle/serialization/json.hh>

#include <reactor/network/unix-domain-server.hh>
#include <reactor/network/unix-domain-socket.hh>

#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("infinit-daemon");

#include <main.hh>


std::string self_path;
struct MountOptions
{
  MountOptions();
  MountOptions(elle::serialization::SerializerIn& s);
  void
  serialize(elle::serialization::Serializer& s);
  void to_commandline(std::vector<std::string>& arguments,
                      std::unordered_map<std::string, std::string>& env) const;
  std::string volume;
  boost::optional<std::string> hub_url;
  boost::optional<std::string> rdv;
  boost::optional<std::vector<std::string>> fuse_options;
  boost::optional<std::string> as;
  boost::optional<bool> fetch;
  boost::optional<bool> push;
  boost::optional<bool> cache;
  boost::optional<bool> async;
  boost::optional<bool> readonly;
  boost::optional<uint64_t> cache_ram_size;
  boost::optional<uint64_t> cache_ram_ttl;
  boost::optional<uint64_t> cache_ram_invalidation;
  boost::optional<uint64_t> cache_disk_size;
  boost::optional<std::string> mountpoint;
  boost::optional<std::vector<std::string>> peers;
  typedef infinit::serialization_tag serialization_tag;
};

MountOptions::MountOptions()
{
}


MountOptions::MountOptions(elle::serialization::SerializerIn& s)
{
  serialize(s);
}

void
MountOptions::serialize(elle::serialization::Serializer& s)
{
  s.serialize("volume", volume);
  s.serialize("hub_url", hub_url);
  s.serialize("rdv", rdv);
  s.serialize("fuse_options", fuse_options);
  s.serialize("fetch", fetch);
  s.serialize("push", push);
  s.serialize("cache", cache);
  s.serialize("async", async);
  s.serialize("readonly", readonly);
  s.serialize("cache_ram_size", cache_ram_size);
  s.serialize("cache_ram_ttl", cache_ram_ttl);
  s.serialize("cache_ram_invalidation", cache_ram_invalidation);
  s.serialize("cache_disk_size", cache_disk_size);
  s.serialize("mountpoint", mountpoint);
  s.serialize("as", as);
  s.serialize("peers", peers);
}

void
MountOptions::to_commandline(std::vector<std::string>& arguments,
                             std::unordered_map<std::string, std::string>& env) const
{
  if (rdv)
    env.insert(std::make_pair("INFINIT_RDV", rdv.get()));
  if (hub_url)
    env.insert(std::make_pair("INFINIT_BEYOND", hub_url.get()));
  arguments.push_back("--run");
  arguments.push_back(volume);
  if (fuse_options)
    for (auto const& fo: fuse_options.get())
    {
      arguments.push_back("--fuse-option");
      arguments.push_back(fo);
    }
  if (peers)
    for (auto const& fo: peers.get())
    {
      arguments.push_back("--peer");
      arguments.push_back(fo);
    }
  if (fetch && *fetch) arguments.push_back("--fetch");
  if (push && *push) arguments.push_back("--push");
  if (cache && *cache) arguments.push_back("--cache");
  if (async && *async) arguments.push_back("--async");
  if (readonly && *readonly) arguments.push_back("--readonly");
  if (cache_ram_size) {arguments.push_back("--cache-ram-size"); arguments.push_back(std::to_string(cache_ram_size.get()));}
  if (cache_ram_ttl) {arguments.push_back("--cache-ram-ttl"); arguments.push_back(std::to_string(cache_ram_ttl.get()));}
  if (cache_ram_invalidation) {arguments.push_back("--cache-ram-invalidation"); arguments.push_back(std::to_string(cache_ram_invalidation.get()));}
  if (cache_disk_size) {arguments.push_back("--cache-disk-size"); arguments.push_back(std::to_string(cache_disk_size.get()));}
  if (mountpoint)
  {
    arguments.push_back("--mountpoint");
    arguments.push_back(mountpoint.get());
  }
  if (as)
  {
    arguments.push_back("--as");
    arguments.push_back(as.get());
  }

}

struct Mount
{
  MountOptions options;
  std::unique_ptr<elle::system::Process> process;
};

class MountManager
{
public:
  void create(std::string const& name, MountOptions const& options);
  void remove(std::string const& name);
  void mount(std::string const& name);
  std::string mount(boost::optional<std::string> name, MountOptions const& options);
  void umount(std::string const& name);
  void status(boost::optional<std::string> name,
              elle::serialization::SerializerOut& reply);
private:
  std::unordered_map<std::string, Mount> _mounts;
  int _next_id;
};

static
MountManager&
manager()
{
  static MountManager mm;
  return mm;
}

void
MountManager::create(std::string const& name, MountOptions const& options)
{
  auto ser = elle::serialization::json::serialize(options);
  auto path = infinit::xdg_data_home() / "mounts" / name;
  if (boost::filesystem::exists(path))
    throw elle::Exception("mount " + name + " already exists");
  boost::filesystem::create_directories(path.parent_path());
  boost::filesystem::ofstream ofs(path);
  ofs.write(reinterpret_cast<const char*>(ser.contents()), ser.size());
}

void
MountManager::remove(std::string const& name)
{
  auto path = infinit::xdg_data_home() / "mounts" / name;
  if (!boost::filesystem::exists(path))
    throw elle::Exception("mount " + name + " does not exist");
  boost::filesystem::remove(path);
}

void
MountManager::mount(std::string const& name)
{
  auto path = infinit::xdg_data_home() / "mounts" / name;
  if (!boost::filesystem::exists(path))
    throw elle::Exception("mount " + name + " does not exist");
  boost::filesystem::ifstream ifs(path);
  auto mo = elle::serialization::json::deserialize<MountOptions>(ifs);
  mount(name, mo);
}

std::string
MountManager::mount(boost::optional<std::string> name, MountOptions const& options)
{
  if (!name)
    name = "mount_" + std::to_string(++_next_id);
  Mount m;
  m.options = options;
  if (m.options.mountpoint && m.options.mountpoint.get() == "auto")
    m.options.mountpoint = (boost::filesystem::temp_directory_path()
    / boost::filesystem::unique_path()).string();
  std::vector<std::string> arguments;
  arguments.push_back(self_path + "/infinit-volume");
  std::unordered_map<std::string, std::string> env;
  m.options.to_commandline(arguments, env);
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
  _mounts.insert(std::make_pair(name.get(), std::move(m)));
  return name.get();
}

void
MountManager::umount(std::string const& name)
{
  auto it = _mounts.find(name);
  if (it == _mounts.end())
    throw elle::Exception("not mounted: " + name);
  kill(it->second.process->pid(), SIGTERM);
  _mounts.erase(it);
}

void MountManager::status(boost::optional<std::string> name,
                          elle::serialization::SerializerOut& reply)
{
  auto it = _mounts.find(name.get());
  if (it == _mounts.end())
    throw elle::Exception("not mounted: " + name.get());
  bool live = ! kill(it->second.process->pid(), 0);
  reply.serialize("live", live);
  if (it->second.options.mountpoint)
    reply.serialize("mountpoint", it->second.options.mountpoint.get());
}

static
std::string
daemon_command(std::string const& s);

static
boost::filesystem::path
pidfile_path()
{
  return infinit::xdg_runtime_dir () / "daemon.pid";
}

static
boost::filesystem::path
sock_path()
{
  return infinit::xdg_runtime_dir() / "daemon.sock";
}

static
int
daemon_pid()
{
  int pid = -1;
  boost::filesystem::ifstream ifs(pidfile_path());
  ifs >> pid;
  return pid;
}

static
bool
daemon_running()
{
  int pid = daemon_pid();
  if (pid == -1)
    return false;
  int res = kill(pid, 0);
  if (res != 0)
    return false;
  try
  {
    daemon_command("{\"operation\": \"status\"}");
    return true;
  }
  catch (elle::Error const& e)
  {
    ELLE_TRACE("status command threw %s", e);
  }
  return false;
}

static
void
daemon_stop()
{
  int pid = daemon_pid();
  if (!daemon_running() || pid == -1)
    elle::err("daemon is not running");
  try
  {
    daemon_command("{\"operation\": \"stop\"}");
  }
  catch (elle::Error const& e)
  {
    ELLE_TRACE("stop command threw %s", e);
  }
  for (int i=0; i<50; ++i)
  {
    if (kill(pid, 0))
      return;
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
      reactor::network::UnixDomainSocket sock(sock_path());
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
process_command(elle::json::Object query)
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
        exit(0);
      }
      else if (op == "create")
      {
        MountOptions mo(command);
        std::string name;
        command.serialize("name", name);
        manager().create(name, mo);
        response.serialize("result", "Ok");
      }
      else if (op == "remove")
      {
        std::string name;
        command.serialize("name", name);
        manager().remove(name);
        response.serialize("result", "Ok");
      }
      else if (op == "mount")
      {
        std::string name;
        command.serialize("name", name);
        manager().mount(name);
        response.serialize("result", "Ok");
      }
      else if (op == "mount_volume")
      {
        MountOptions mo(command);
        boost::optional<std::string> name;
        command.serialize("name", name);
        name = manager().mount(name, mo);
        response.serialize("name", name);
        response.serialize("result", "Ok");
      }
      else if (op == "umount")
      {
        std::string name;
        command.serialize("name", name);
        manager().umount(name);
        response.serialize("result", "Ok");
      }
      else if (op == "mount_status")
      {
        std::string name;
        command.serialize("name", name);
        manager().status(name, response);
        response.serialize("result", "Ok");
      }
      else
      {
        response.serialize("error", "Unknown operatior: " + op);
      }
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

COMMAND(start)
{
  if (daemon_running())
    elle::err("daemon already running");
  if (!flag(args, "foreground"))
    daemonize();
  {
    boost::filesystem::ofstream ofs(pidfile_path());
    ofs << getpid();
  }
  reactor::network::UnixDomainServer srv;
  auto sockaddr = sock_path();
  boost::filesystem::remove(sockaddr);
  srv.listen(sockaddr);
  elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
  {
    while (true)
    {
      auto socket = elle::utility::move_on_copy(srv.accept());
      auto name = elle::sprintf("%s server", **socket);
      scope.run_background(
        name,
        [socket]
        {
          try
          {
            while (true)
            {
              auto json =
                boost::any_cast<elle::json::Object>(elle::json::read(**socket));
              auto reply = process_command(json);
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

int
main(int argc, char** argv)
{
  if (argv[0][0] == '/')
    self_path = boost::filesystem::path(argv[0]).parent_path().string();
  else
  {
    char cwd[4096];
    getcwd(cwd, 4096);
    self_path = boost::filesystem::path(std::string(cwd) + "/" + argv[0])
      .parent_path().string();
  }
  std::string arg1(argv[1]);
  bool dashed = true;
  auto commands = {"start", "stop", "status"};
  // Accept mode argument without a leading '--'
  if (arg1[0] != '-')
  {
    if (std::find(commands.begin(), commands.end(), arg1) != commands.end())
      arg1 = "--" + arg1;
    else
      dashed = false;
    argv[1] = const_cast<char*>(arg1.c_str());
  }
  if (!dashed)
  {
    // Assume query to be sent to daemon
    if (!daemon_running())
      elle::err("daemon is not running");
    std::string cmd;
    if (arg1[0] == '{')
      cmd = arg1;
    else
    {
      elle::json::Object obj;
      obj.insert(std::make_pair("operation", arg1));
      for (int i = 2; i < argc; ++i)
      {
        std::string kv = argv[i];
        auto p = kv.find_first_of('=');
        if (p == kv.npos)
          obj.insert(std::make_pair(kv, true));
        else
        {
          std::string key = kv.substr(0, p);
          std::string val = kv.substr(p+1);
          if (val == "true")
            obj.insert(std::make_pair(key, true));
          else if (val == "false")
            obj.insert(std::make_pair(key, false));
          else
          {
            try
            {
              std::size_t pos;
              int iv = std::stoi(val, &pos);
              if (pos != val.size())
                throw std::runtime_error("stoi failure");
              obj.insert(std::make_pair(key, iv));
            }
            catch (std::exception const& e)
            {
              if (val.find(',') != val.npos)
              {
                std::vector<std::string> vals;
                boost::algorithm::split(vals, val, boost::is_any_of(","),
                                        boost::token_compress_on);
                if (vals.back().empty())
                  vals.pop_back();
                elle::json::Array jvals(vals.begin(), vals.end());
                obj.insert(std::make_pair(key, jvals));
              }
              else
                obj.insert(std::make_pair(key, val));
            }
          }
        }
      }
      std::stringstream ss;
      elle::json::write(ss, obj, false);
      cmd = ss.str();
      ELLE_TRACE("Parsed command: '%s'", cmd);
    }
    std::cout << daemon_command(cmd) << std::endl;
    return 0;
  }
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "status",
      "Query daemon status",
      &status,
      "",
      {}
    },
    {
      "start",
      "Start daemon",
      &start,
      "",
      {
        { "foreground,f", bool_switch(), "do not daemonize" },
      }
    },
    {
      "stop",
      "Stop daemon",
      &stop,
      "",
      {}
    },
  };
  return infinit::main("Infinit daemon", modes, argc, argv);
}
