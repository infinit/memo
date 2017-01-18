#include <infinit/cli/Daemon.hh>

#include <stdlib.h> // ::daemon

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/Exit.hh>
#include <elle/log.hh>
#include <elle/make-vector.hh>
#include <elle/system/PIDFile.hh>
#include <elle/system/Process.hh>
#include <elle/system/unistd.hh>
#include <elle/serialization/json.hh>
#include <elle/system/self-path.hh>

#include <reactor/network/http-server.hh>
#include <reactor/network/unix-domain-server.hh>
#include <reactor/network/unix-domain-socket.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/MountManager.hh>
#include <infinit/cli/utility.hh>

ELLE_LOG_COMPONENT("cli.daemon");

namespace bfs = boost::filesystem;

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Daemon::Daemon(Infinit& infinit)
      : Entity(infinit)
      , status(
        "Query daemon status",
        das::cli::Options(),
        this->bind(modes::mode_status))
      , stop(
        "Stop daemon",
        das::cli::Options(),
        this->bind(modes::mode_stop))
    {}

    // daemon.
    namespace
    {
      class PIDFile
        : public elle::PIDFile
      {
      public:
        PIDFile()
          : elle::PIDFile(this->path())
        {}

        static
        bfs::path
        root_path()
        {
          auto res = bfs::path("/run/user/0/infinit/filesystem/daemon.pid");
          if (getuid() == 0 && bfs::create_directories(res.parent_path()))
            bfs::permissions(res.parent_path(), bfs::add_perms | bfs::others_read);
          return res;
        }

        static
        bfs::path
        path()
        {
          if (getuid() == 0)
            return root_path();
          else
            return infinit::xdg_runtime_dir() / "daemon.pid";
        }

        static
        int
        read()
        {
          return elle::PIDFile::read(PIDFile::path());
        }
      };

      elle::serialization::json::SerializerIn
      cmd_response_serializer(elle::json::Object const& json,
                              std::string const& action)
      {
        auto s = elle::serialization::json::SerializerIn(json, false);
        if (json.count("error"))
          elle::err("Unable to %s: %s", action, s.deserialize<std::string>("error"));
        return s;
      }

      elle::json::Object
      daemon_command(std::string const& s, bool hold = false);

      bool
      daemon_running()
      {
        try
        {
          daemon_command("{\"operation\": \"status\"}");
          return true;
        }
        catch (elle::Error const& e)
        {
          ELLE_TRACE("status command threw %s", e);
          return false;
        }
      }

      int
      daemon_pid(bool ensure_running = false)
      {
        int pid = 0;
        try
        {
          // FIXME: Try current user, then root. Must be a better way to do this.
          try
          {
            pid = PIDFile::read();
          }
          catch (elle::Error const&)
          {
            pid = elle::PIDFile::read(PIDFile::root_path());
          }
          if (ensure_running && !daemon_running())
            pid = 0;
        }
        catch (elle::Error const& e)
        {
          ELLE_TRACE("error getting PID from %s and %s: %s",
                     PIDFile::path(), PIDFile::root_path(), e);
        }
        return pid;
      }

      void
      daemon_stop()
      {
        int pid = daemon_pid(true);
        if (!pid)
          elle::err("daemon is not running");
        try
        {
          auto json = daemon_command("{\"operation\": \"stop\"}");
          cmd_response_serializer(json, "stop daemon");
        }
        catch (reactor::network::ConnectionClosed const&)
        {}
        auto signal_and_wait_exit = [pid] (boost::optional<int> signal = {}) {
          if (kill(pid, 0) && errno == EPERM)
            elle::err("permission denied");
          if (signal)
          {
            ELLE_TRACE("sending %s", strsignal(*signal));
            if (kill(pid, *signal))
              return false;
            std::cout << "daemon signaled, waiting..." << std::endl;
          }
          int count = 0;
          while (++count < 150)
          {
            if (kill(pid, 0) && errno == ESRCH)
              return true;
            usleep(100000);
          }
          return false;
        };
        if (!signal_and_wait_exit()
            && !signal_and_wait_exit(SIGTERM)
            && !signal_and_wait_exit(SIGKILL))
          elle::err("unable to stop daemon");
        std::cout << "daemon stopped" << std::endl;
      }

      void
      daemonize()
      {
        if (::daemon(1, 0))
          elle::err("failed to daemonize: %s", strerror(errno));
      }

      elle::json::Object
      daemon_command(std::string const& s, bool hold)
      {
        reactor::Scheduler sched;
        elle::json::Object res;
        reactor::Thread main_thread(
          sched,
          "main",
          [&]
          {
            // try local then global
            std::unique_ptr<reactor::network::UnixDomainSocket> sock = [] {
              try
              {
                try
                {
                  return std::make_unique<reactor::network::UnixDomainSocket>(
                    daemon_sock_path());
                }
                catch (elle::Error const&)
                {
                  return std::make_unique<reactor::network::UnixDomainSocket>(
                    bfs::path("/tmp/infinit-root/daemon.sock"));
                }
              }
              catch (reactor::network::ConnectionRefused const&)
              {
                elle::err("ensure that an instance of infinit-daemon is running");
              }
            }();
            std::string cmd = s + "\n";
            ELLE_TRACE("writing query: %s", s);
            sock->write(elle::ConstWeakBuffer(cmd));
            ELLE_TRACE("reading result");
            auto buffer = sock->read_until("\n");
            auto stream = elle::IOStream(buffer.istreambuf());
            res = boost::any_cast<elle::json::Object>(elle::json::read(stream));
            if (hold)
              reactor::sleep();
          });
        sched.run();
        return res;
      }

      void
      restart_volume(MountManager& manager,
                     std::string const& volume, bool always_start = false)
      {
        infinit::MountOptions mo;
        try
        {
          mo.mountpoint = manager.mountpoint(volume, true);
        }
        catch (elle::Error const&)
        {}
        try
        {
          manager.stop(volume);
        }
        catch (elle::Error const& e)
        {
          if (!always_start)
            throw;
        }
        reactor::sleep(5_sec);
        manager.start(volume, mo, false, true);
      }

      std::string
      process_command(elle::json::Object query, MountManager& manager,
                      std::function<void()>& on_end)
      {
        ELLE_TRACE("command: %s", elle::json::pretty_print(query));
        auto command = elle::serialization::json::SerializerIn(query, false);
        std::stringstream ss;
        {
          auto response = elle::serialization::json::SerializerOut(ss, false);
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
              if (getuid() == geteuid())
                throw elle::Exit(0);
              else
                elle::err("permission denied");
            }
            else if (op == "volume-list")
            {
              auto res = manager.list();
              response.serialize("volumes", res);
            }
            else if (op == "volume-status")
            {
              if (auto volume
                  = command.deserialize<boost::optional<std::string>>("volume"))
                manager.status(*volume, response);
              else
                response.serialize("volumes", manager.status());
            }
            else if (op == "volume-start")
            {
              auto volume = command.deserialize<std::string>("volume");
              auto opts =
                command.deserialize<boost::optional<infinit::MountOptions>>("options");
              manager.start(volume, opts ? *opts : infinit::MountOptions(),
                            true, true);
              response.serialize("volume", volume);
            }
            else if (op == "volume-stop")
            {
              auto volume = command.deserialize<std::string>("volume");
              manager.stop(volume);
              response.serialize("volume", volume);
            }
            else if (op == "volume-restart")
            {
              auto volume = command.deserialize<std::string>("volume");
              restart_volume(manager, volume);
              response.serialize("volume", volume);
            }
            else if (op == "disable-storage")
            {
              auto volume = command.deserialize<std::string>("volume");
              ELLE_LOG("Disabling storage on %s", volume);
              auto opts = elle::json::Object {
                {"no-storage", std::string{}},
              };
              manager.create_volume(volume, opts);
              restart_volume(manager, volume);
            }
            else if (op == "enable-storage")
            {
              auto volume = command.deserialize<std::string>("volume");
              ELLE_LOG("Enabling storage on %s", volume);
              auto opts = elle::json::Object {
                {"storage", std::string{}},
              };
              manager.create_volume(volume, opts);
              restart_volume(manager, volume, true);
              if (command.deserialize<bool>("hold"))
                on_end = [volume, &manager]() {
                  ELLE_LOG("Disabling storage on %s", volume);
                  auto opts = elle::json::Object {
                    {"no-storage", std::string{}}
                  };
                  manager.create_volume(volume, opts);
                  restart_volume(manager, volume);
                };
            }
            else
            {
              elle::err("unknown operation: %s", op);
            }
            response.serialize("result", "Ok");
          }
          catch (elle::Error const& e)
          {
            response.serialize("result", "Error");
            response.serialize("error", e.what());
          }
        }
        ss << '\n';
        return ss.str();
      }
    }

    /*---------------.
    | Mode: status.  |
    `---------------*/
    void
    Daemon::mode_status()
    {
      ELLE_TRACE_SCOPE("status");
      std::cout << (daemon_running() ? "Running" : "Stopped") << std::endl;
    }

    /*-------------.
    | Mode: stop.  |
    `-------------*/
    void
    Daemon::mode_stop()
    {
      ELLE_TRACE_SCOPE("stop");
      daemon_stop();
    }
  }
}
