#include <sys/types.h>
#include <signal.h>

#include <boost/filesystem.hpp>

#include <elle/log.hh>

#include <reactor/network/tcp-server.hh>
#include <reactor/network/tcp-socket.hh>

ELLE_LOG_COMPONENT("infinit-daemon");

#include <main.hh>

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
int
daemon_port()
{
  int pid = -1;
  std::ifstream ifs((xdg_run() /"infinit-daemon"/"port").string());
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
  return res == 0;
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
  ELLE_TRACE("Sending TERM to %s", pid);
  kill(pid, SIGTERM);
  for (int i=0; i<50; ++i)
  {
    if (!kill(pid, 0))
      return;
    usleep(100000);
  }
  ELLE_TRACE("Process still running, sending KILL");
  kill(pid, SIGKILL);
  for (int i=0; i<50; ++i)
  {
    if (!kill(pid, 0))
      return;
    usleep(100000);
  }
}

static
void
daemonize()
{
  if (daemon(1, 0))
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
      reactor::network::TCPSocket sock("localhost", daemon_port());
      std::string cmd = s + "\n";
      ELLE_TRACE("writing query: %s", s);
      sock.write(elle::ConstWeakBuffer(cmd.data(), cmd.size()));
      ELLE_TRACE("reading result");
      reply = sock.read_until("\n").string();
      ELLE_TRACE("ok: %s", reply);
    });
  sched.run();
  return reply;
}

static
std::string
process_command(std::string const& c)
{
  return "OK";
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
  reactor::network::TCPServer srv;
  srv.listen(0);
  {
    std::ofstream of((xdg_run() / "infinit-daemon" / "port").string());
    of << srv.port();
  }
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
              ELLE_TRACE("acquired socket, waiting for command");
              auto command = socket->read_until("\n").string();
              ELLE_TRACE("Processing command");
              auto reply = process_command(command) + "\n";
              ELLE_TRACE("sending result");
              socket->write(reply);
              ELLE_TRACE("done");
            }
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("%s", e);
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
    std::vector<std::string> sargs(argv+1, argv + argc);
    auto cmd = boost::algorithm::join(sargs, " ");
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