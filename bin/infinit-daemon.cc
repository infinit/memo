#include <sys/types.h>
#include <signal.h>

#include <boost/filesystem.hpp>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <reactor/network/unix-domain-server.hh>
#include <reactor/network/unix-domain-socket.hh>

ELLE_LOG_COMPONENT("infinit-daemon");

#include <main.hh>

static
std::string
daemon_command(std::string const& s);

static
boost::filesystem::path
xdg_run()
{
  return elle::os::getenv("XDG_RUNTIME_DIR",
    (infinit::home() / ".local" / "run").string());
}

static
int
daemon_pid()
{
  int pid = -1;
  std::ifstream ifs((xdg_run() /"infinit-daemon"/"pid").string());
  ifs >> pid;
  return pid;
}

static
boost::filesystem::path
daemon_port()
{
  return xdg_run() /"infinit-daemon"/"sock";
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
  {
    std::cerr << "Daemon is not running." << std::endl;
    return;
  }
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
  if (elle::os::getenv("INFINIT_NO_DAEMON", "").empty() && daemon(1, 0))
    throw elle::Error(elle::sprintf("daemon failed with %s", strerror(errno)));
  auto run = xdg_run();
  boost::filesystem::create_directories(run / "infinit-daemon");
  std::ofstream ofs((run / "infinit-daemon" / "pid").string());
  ofs << getpid();
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
      reactor::network::UnixDomainSocket sock(daemon_port());
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
    if (op == "status")
    {
      response.serialize("status", "Ok");
    }
    else if (op == "stop")
    {
      exit(0);
    }
    else
    {
      response.serialize("error", "Unknown operatior: " + op);
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
  {
    std::cerr << "Daemon already running." << std::endl;
    return;
  }
  daemonize();
  reactor::network::UnixDomainServer srv;
  auto sockaddr = xdg_run() / "infinit-daemon" / "sock";
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

int main(int argc, char** argv)
{
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
    {
      std::cerr << "Daemon is not running." << std::endl;
      return 1;
    }
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
          obj.insert(std::make_pair(kv, std::string()));
        else
          obj.insert(std::make_pair(kv.substr(0, p), kv.substr(p+1)));
      }
      std::stringstream ss;
      elle::json::write(ss, obj, false);
      cmd = ss.str();
      ELLE_TRACE("Parsed command: '%s'", cmd);
    }
    try
    {
      std::cout << daemon_command(cmd) << std::endl;
    }
    catch (elle::Error const& e)
    {
      std::cerr << e.what() << std::endl;
      return 1;
    }
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
      {}
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