#include <sys/types.h>
#include <signal.h>

#include <boost/filesystem.hpp>

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <reactor/network/unix-domain-server.hh>
#include <reactor/network/unix-domain-socket.hh>

#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("infinit-daemon");

#include <main.hh>

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
          std::string val = kv.substr(p+1);
          if (val == "true")
            obj.insert(std::make_pair(kv.substr(0, p), true));
          else if (val == "false")
            obj.insert(std::make_pair(kv.substr(0, p), false));
          else
          {
            try
            {
              obj.insert(std::make_pair(kv.substr(0, p), std::stoi(val)));
            }
            catch (std::exception const& e)
            {
              obj.insert(std::make_pair(kv.substr(0, p), val));
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
