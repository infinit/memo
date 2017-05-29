#include <infinit/cli/Daemon.hh>

#include <cstdlib> // ::daemon
#include <pwd.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/Exit.hh>
#include <elle/log.hh>
#include <elle/make-vector.hh>
#include <elle/serialization/json.hh>
#include <elle/system/PIDFile.hh>
#include <elle/system/Process.hh>
#include <elle/system/self-path.hh>
#include <elle/system/unistd.hh>

#include <elle/reactor/network/http-server.hh>
#include <elle/reactor/network/unix-domain-server.hh>
#include <elle/reactor/network/unix-domain-socket.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>
#include <infinit/silo/Filesystem.hh>

ELLE_LOG_COMPONENT("cli.daemon");

namespace bfs = boost::filesystem;

namespace infinit
{
  using Passport = infinit::model::doughnut::Passport;

  namespace cli
  {
    using Strings = Daemon::Strings;

    Daemon::Daemon(Infinit& infinit)
      : Object(infinit)
      , disable_storage(*this,
                        "Disable storage on associated network",
                        cli::name)
        // XXX[Storage]: Should we keep storage ?
      , enable_storage(*this,
                       "Enable storage on associated network",
                       cli::name,
                       cli::hold)
      , fetch(*this,
              "Fetch network and its dependencies from {hub}",
              cli::name)
      , run(*this,
            "Run daemon in the foreground",
            cli::login_user = Strings{},
            cli::default_network = boost::none,
            cli::advertise_host = Strings{},
            cli::fetch = false,
            cli::push = false,
#ifdef WITH_DOCKER
            cli::docker = true,
            cli::docker_user = boost::none,
            cli::docker_home = boost::none,
            cli::docker_socket_tcp = false,
            cli::docker_socket_port = 0,
            cli::docker_socket_path = "/run/docker/plugins",
            cli::docker_descriptor_path = "/usr/lib/docker/plugins",
            cli::docker_mount_substitute = "",
#endif
            cli::log_level = boost::none,
            cli::log_path = boost::none)
      , start(*this,
              "Start daemon in the background",
              cli::login_user = Strings{},
              cli::default_network = boost::none,
              cli::advertise_host = Strings{},
              cli::fetch = false,
              cli::push = false,
#ifdef WITH_DOCKER
              cli::docker = true,
              cli::docker_user = boost::none,
              cli::docker_home = boost::none,
              cli::docker_socket_tcp = false,
              cli::docker_socket_port = 0,
              cli::docker_socket_path = "/run/docker/plugins",
              cli::docker_descriptor_path = "/usr/lib/docker/plugins",
              cli::docker_mount_substitute = "",
#endif
              cli::log_level = boost::none,
              cli::log_path = boost::none)
      , status(*this, "Query daemon status")
      , stop(*this, "Stop daemon")
    {}


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

      boost::optional<std::string>
      optional(elle::json::Object const& options, std::string const& name)
      {
        auto it = options.find(name);
        if (it == options.end())
          return {};
        else
          return boost::any_cast<std::string>(it->second);
      }

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
        catch (elle::reactor::network::ConnectionClosed const&)
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
        elle::reactor::Scheduler sched;
        elle::json::Object res;
        elle::reactor::Thread daemon_query(
          sched,
          "daemon-query",
          [&]
          {
            // try local then global
            std::unique_ptr<elle::reactor::network::UnixDomainSocket> sock = [] {
              try
              {
                try
                {
                  return std::make_unique<elle::reactor::network::UnixDomainSocket>(
                    daemon_sock_path());
                }
                catch (elle::Error const&)
                {
                  return std::make_unique<elle::reactor::network::UnixDomainSocket>(
                    bfs::path("/tmp/infinit-root/daemon.sock"));
                }
              }
              catch (elle::reactor::network::ConnectionRefused const&)
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
              elle::reactor::sleep();
          });
        sched.run();
        return res;
      }

      std::string
      process_command(elle::json::Object query,
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

      struct SystemUser
      {
        SystemUser(unsigned int uid,
                   unsigned int gid,
                   std::string name,
                   std::string home)
          : uid(uid)
          , gid(gid)
          , name(name)
          , home(home)
        {}

        SystemUser(unsigned int uid, boost::optional<std::string> home_ = {})
          : uid(uid)
          , gid(0)
        {
          passwd* pwd = getpwuid(uid);
          if (!pwd)
            elle::err("No user found with uid %s", uid);
          name = pwd->pw_name;
          home = home_.value_or(pwd->pw_dir);
          gid = pwd->pw_gid;
        }

        SystemUser(std::string const& name_, boost::optional<std::string> home_ = {})
        {
          passwd* pwd = getpwnam(name_.c_str());
          if (!pwd)
            elle::err("No user found with name %s", name_);
          name = pwd->pw_name;
          home = home_.value_or(pwd->pw_dir);
          uid = pwd->pw_uid;
          gid = pwd->pw_gid;
        }
        unsigned int uid;
        unsigned int gid;
        std::string name;
        std::string home;

        struct Lock
          : public elle::reactor::Lock
        {
          Lock(SystemUser const& su, elle::reactor::Lockable& l)
            : elle::reactor::Lock(l)
          {
            elle::os::setenv("MEMO_HOME", su.home);
            if (!elle::os::getenv("MEMO_HOME_OVERRIDE", "").empty())
              elle::os::setenv("MEMO_HOME",
                elle::os::getenv("MEMO_HOME_OVERRIDE", ""));
            elle::os::unsetenv("MEMO_DATA_HOME");
            elle::setegid(su.gid);
            elle::seteuid(su.uid);
          }

          Lock(Lock const& b) = delete;

          Lock(Lock && b) = default;

          ~Lock()
          {
            if (prev_home.empty())
              elle::os::unsetenv("MEMO_HOME");
            else
              elle::os::setenv("MEMO_HOME", prev_home);
            if (!prev_data_home.empty())
              elle::os::setenv("MEMO_DATA_HOME", prev_data_home);
            elle::seteuid(prev_euid);
            elle::setegid(prev_egid);
          }

          std::string const prev_home = elle::os::getenv("MEMO_HOME", "");
          std::string const prev_data_home = elle::os::getenv("MEMO_DATA_HOME", "");
          int const prev_euid = geteuid();
          int const prev_egid = getegid();
        };

        Lock
        enter(elle::reactor::Lockable& l) const
        {
          return {*this, l};
        }
      };

      void
      _run(infinit::Infinit& ifnt,
           Infinit& cli,
           Strings const& login_user,
           boost::optional<std::string> const& default_network,
           Strings const& advertise_host,
           bool fetch,
           bool push,
#ifdef WITH_DOCKER
           bool docker,
           boost::optional<std::string> const& docker_user,
           boost::optional<std::string> const& docker_home,
           bool docker_socket_tcp,
           int const& docker_socket_port,
           std::string const& docker_socket_path,
           std::string const& docker_descriptor_path,
           std::string const& docker_mount_substitute,
#endif
           boost::optional<std::string> const& log_level,
           boost::optional<std::string> const& log_path,
           bool detach)
      {
//         auto owner = cli.as_user();
//         // Pass options to `manager`.
//         auto fill_manager_options = [&](MountManager& manager)
//           {
//             manager.log_level(log_level);
//             manager.log_path(log_path);
//             manager.fetch(fetch);
//             manager.push(push);
//             manager.default_user(owner.name);
//             manager.default_network(default_network);
//             manager.advertise_host(advertise_host);
//           };

//         ELLE_TRACE("starting daemon");
//         if (daemon_running())
//           elle::err("daemon already running");
//         auto system_user = [&] {
//           if (docker_user)
//             return SystemUser(*docker_user, docker_home);
//           else
//             return SystemUser(getuid(), docker_home);
//         }();
//         elle::reactor::Mutex mutex;
//         // uid -> manager.
//         auto managers = std::unordered_map<int, std::unique_ptr<MountManager>>{};
//         auto dvp = std::unique_ptr<DockerVolumePlugin>{};
//         auto mounter = std::unique_ptr<elle::reactor::Thread>{};
//         // Always call get_mount_root() before entering the SystemUser.
//         auto get_mount_root = [&] (SystemUser const& user) {
//           if (mount_root)
//           {
//             auto res = bfs::path(*mount_root);
//             if (!bfs::exists(res))
//               elle::err("mount root does not exist");
//             if (!bfs::is_directory(res))
//               elle::err("mount root is not a directory");
//             // Add the uid so that we have a directory for each user.
//             res /= std::to_string(user.uid);
//             if (bfs::create_directories(res))
//             {
//               elle::chown(res.string(), user.uid, user.gid);
//               bfs::permissions(res.string(), bfs::owner_all);
//             }
//             return bfs::canonical(res).string();
//           }
//           else
//           {
//             auto env_run_dir = elle::os::getenv("XDG_RUNTIME_DIR", "");
//             auto user_run_dir = !env_run_dir.empty()
//               ? env_run_dir : elle::sprintf("/run/user/%s", user.uid);
//             if (bfs::create_directories(user_run_dir))
//             {
//               elle::chown(user_run_dir, user.uid, user.gid);
//               bfs::permissions(user_run_dir, bfs::owner_all);
//             }
//             {
//               if (mutex.locked())
//                 elle::err("call get_mount_root() before .enter() on the SystemUser");
//               auto lock = user.enter(mutex);
//               auto res = elle::sprintf("%s/infinit/filesystem/mnt", user_run_dir);
//               bfs::create_directories(res);
//               return res;
//             }
//           }
//         };
//         auto user_mount_root = get_mount_root(system_user);
//         // Scope for lock.
//         {
//           auto lock = system_user.enter(mutex);
//           for (auto const& u: login_user)
//           {
//             auto sep = u.find(':');
//             auto name = u.substr(0, sep);
//             auto pass = u.substr(sep+1);
//             auto c = LoginCredentials{name, Infinit::hub_password_hash(pass)};
//             auto json = ifnt.beyond_login(name, c);
//             elle::serialization::json::SerializerIn input(json, false);
//             auto user = input.deserialize<infinit::User>();
//             ifnt.user_save(user, true);
//           }
//           ELLE_TRACE("starting initial manager");
//           managers[getuid()].reset(new MountManager(ifnt,
//                                                     user_mount_root,
//                                                     docker_mount_substitute));
//           MountManager& root_manager = *managers[getuid()];
//           fill_manager_options(root_manager);
//           dvp = std::make_unique<DockerVolumePlugin>(
//             root_manager, system_user, mutex);
//           if (!mount.empty())
//             mounter = std::make_unique<elle::reactor::Thread>("mounter",
//               [&] {auto_mounter(mount, *dvp);});
//         }
//         if (docker)
//         {
// #ifdef WITH_DOCKER
//           try
//           {
//             dvp->install(docker_socket_tcp, docker_socket_port,
//                          docker_socket_path, docker_descriptor_path);
//           }
//           catch (std::exception const& e)
//           {
//             ELLE_ERR("Failed to install docker plugin: %s", e.what());
//             ELLE_ERR("Docker plugin disabled");
//           }
// #endif
//         }
//         if (detach)
//           daemonize();
//         PIDFile pid;
//         elle::reactor::network::UnixDomainServer srv;
//         auto sockaddr = daemon_sock_path();
//         bfs::remove(sockaddr);
//         srv.listen(sockaddr);
//         chmod(sockaddr.string().c_str(), 0666);
//         elle::SafeFinally terminator([&] {
//           if (mounter)
//             mounter->terminate_now();
//           ELLE_LOG("stopped daemon");
//         });
//         elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& scope)
//         {
//           ELLE_LOG("started daemon");
//           while (true)
//           {
//             auto socket = elle::utility::move_on_copy(srv.accept());
//             auto native = socket->socket()->native();
//             uid_t uid;
//             gid_t gid;
// #ifdef INFINIT_MACOSX
//             if (getpeereid(native, &uid, &gid))
//             {
//               ELLE_ERR("getpeerid failed: %s", strerror(errno));
//               continue;
//             }
// #elif defined INFINIT_LINUX
//             struct ucred ucred;
//             socklen_t len = sizeof ucred;
//             if (getsockopt(native, SOL_SOCKET, SO_PEERCRED, &ucred, &len) == -1)
//             {
//                ELLE_ERR("getsocopt(peercred) failed: %s", strerror(errno));
//                continue;
//             }
//             uid = ucred.uid;
//             gid = ucred.gid;
// #else
// # error "unsupported platform"
// #endif
//             static elle::reactor::Mutex mutex;
//             (void)gid;
//             auto system_user = SystemUser{uid};
//             auto user_manager = [&]{
//               auto it = managers.find(uid);
//               if (it == managers.end())
//                 {
//                   auto peer_mount_root = get_mount_root(system_user);
//                   auto lock = system_user.enter(mutex);
//                   auto res = new MountManager(ifnt,
//                                               peer_mount_root,
//                                               docker_mount_substitute);
//                   fill_manager_options(*res);
//                   managers[uid].reset(res);
//                   return res;
//                 }
//               else
//                 return it->second.get();
//             }();
//             auto name = elle::sprintf("%s server", **socket);
//             scope.run_background(
//               name,
//               [socket, user_manager, system_user]
//               {
//                 auto on_end = std::function<void()>{};
//                 elle::SafeFinally sf([&] {
//                     try
//                     {
//                       if (on_end)
//                         on_end();
//                     }
//                     catch (elle::Error const& e)
//                     {
//                       ELLE_WARN("Unexpected exception in on_end: %s", e);
//                     }
//                 });
//                 try
//                 {
//                   while (true)
//                   {
//                     auto json =
//                       boost::any_cast<elle::json::Object>(elle::json::read(**socket));
//                     auto reply = [&] {
//                       SystemUser::Lock lock(system_user.enter(mutex));
//                       return process_command(json, *user_manager, on_end);
//                     }();
//                     ELLE_TRACE("Writing reply: '%s'", reply);
//                     socket->write(reply);
//                   }
//                 }
//                 catch (elle::Error const& e)
//                 {
//                   ELLE_TRACE("%s", e);
//                   try
//                   {
//                     socket->write(elle::sprintf("{\"error\": \"%s\"}\n", e.what()));
//                   }
//                   catch (elle::Error const&)
//                   {}
//                 }
//               });
//           }
//         };
//       }

      }
    }

    /*------------------------.
    | Mode: disable_storage.  |
    `------------------------*/
    void
    Daemon::mode_disable_storage(std::string const& name)
    {
      std::cout << daemon_command(
        "{\"operation\": \"disable-storage\", \"network\": \"" + name +  "\"}");
    }


    /*-----------------------.
    | Mode: enable_storage.  |
    `-----------------------*/
    void
    Daemon::mode_enable_storage(std::string const& name,
                                bool hold)
    {
      std::cout << daemon_command(
        "{\"operation\": \"enable-storage\", \"network\": \"" + name +  "\""
        +",\"hold\": "  + (hold ? "true": "false")   + "}", hold);
    }


    /*--------------.
    | Mode: fetch.  |
    `--------------*/

    namespace
    {
      std::pair<std::string, infinit::User>
      split(infinit::Infinit& ifnt,
            std::string const& name)
      {
        auto p = name.find_first_of('/');
        if (p == name.npos)
          elle::err("Malformed qualified name");
        return {name.substr(p+1), ifnt.user_get(name.substr(0, p))};
      }

      void link_network(infinit::Infinit& ifnt,
                        std::string const& name,
                        elle::json::Object const& options = elle::json::Object{})
      {
        auto cname = split(ifnt, name);
        auto desc = ifnt.network_descriptor_get(cname.first, cname.second, false);
        auto users = ifnt.users_get();
        auto passport = boost::optional<infinit::Passport>{};
        auto user = boost::optional<infinit::User>{};
        ELLE_TRACE("checking if any user is owner");
        for (auto const& u: users)
          if (u.public_key == desc.owner && u.private_key)
          {
            passport.emplace(u.public_key, desc.name,
              elle::cryptography::rsa::KeyPair(u.public_key,
                                                  u.private_key.get()));
            user.emplace(u);
            break;
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
            catch (infinit::MissingLocalResource const&)
            {
              try
              {
                passport.emplace(ifnt.beyond_fetch<infinit::Passport>(elle::sprintf(
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
          elle::err("Failed to acquire passport.");
        ELLE_TRACE("Passport found for user %s", user->name);

        auto silo_config = [&] () -> std::unique_ptr<infinit::silo::SiloConfig> {
          auto silodesc = optional(options, "silo");
          if (silodesc && silodesc->empty())
          {
            auto siloname = boost::replace_all_copy(name + "_silo", "/", "_");
            ELLE_LOG("Creating local silo %s", siloname);
            auto path = infinit::xdg_data_home() / "blocks" / siloname;
            return
              std::make_unique<infinit::silo::FilesystemSiloConfig>(
                siloname, path.string(), boost::none, boost::none);
          }
          else if (silodesc)
          {
            try
            {
              return ifnt.silo_get(*silodesc);
            }
            catch (infinit::MissingLocalResource const&)
            {
              elle::err("silo specification for new silo not implemented");
            }
          }
          else
            return nullptr;
        }();

        auto network = infinit::Network(
          desc.name,
          std::make_unique<infinit::model::doughnut::Configuration>(
            infinit::model::Address::random(0), // FIXME
            std::move(desc.consensus),
            std::move(desc.overlay),
            std::move(silo_config),
            user->keypair(),
            std::make_shared<elle::cryptography::rsa::PublicKey>(desc.owner),
            std::move(*passport),
            user->name,
            boost::optional<int>(),
            desc.version,
            desc.admin_keys,
            std::vector<infinit::model::Endpoints>(),
            desc.tcp_heartbeat,
            std::move(desc.encrypt_options)),
          boost::none);
        ifnt.network_save(*user, network, true);
        ifnt.network_save(std::move(network), true);
      }

      void
      acquire_network(infinit::Infinit& ifnt,
                      std::string const& name)
      {
        auto desc
          = ifnt.beyond_fetch<infinit::NetworkDescriptor>("network", name);
        ifnt.network_save(desc);
        try
        {
          auto nname = split(ifnt, name);
          auto net = ifnt.network_get(nname.first, nname.second, true);
        }
        catch (elle::Error const&)
        {
          link_network(ifnt, name);
        }
      }
    }

    void
    Daemon::mode_fetch(std::string const& name)
    {
      ELLE_TRACE_SCOPE("fetch");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      acquire_network(ifnt, name);
    }


    /*------------.
    | Mode: run.  |
    `------------*/
    void
    Daemon::mode_run(Strings const& login_user,
                     boost::optional<std::string> const& default_network,
                     Strings const& advertise_host,
                     bool fetch,
                     bool push,
#ifdef WITH_DOCKER
                     bool docker,
                     boost::optional<std::string> const& docker_user,
                     boost::optional<std::string> const& docker_home,
                     bool docker_socket_tcp,
                     int const& docker_socket_port,
                     std::string const& docker_socket_path,
                     std::string const& docker_descriptor_path,
                     std::string const& docker_mount_substitute,
#endif
                     boost::optional<std::string> const& log_level,
                     boost::optional<std::string> const& log_path)
    {
      ELLE_TRACE_SCOPE("run");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      _run(ifnt, cli,
           login_user,
           default_network,
           advertise_host,
           fetch,
           push,
#ifdef WITH_DOCKER
           docker,
           docker_user,
           docker_home,
           docker_socket_tcp,
           docker_socket_port,
           docker_socket_path,
           docker_descriptor_path,
           docker_mount_substitute,
#endif
           log_level,
           log_path,
           false);
    }

    /*--------------.
    | Mode: start.  |
    `--------------*/
    void
    Daemon::mode_start(Strings const& login_user,
                       boost::optional<std::string> const& default_network,
                       Strings const& advertise_host,
                       bool fetch,
                       bool push,
#ifdef WITH_DOCKER
                       bool docker,
                       boost::optional<std::string> const& docker_user,
                       boost::optional<std::string> const& docker_home,
                       bool docker_socket_tcp,
                       int const& docker_socket_port,
                       std::string const& docker_socket_path,
                       std::string const& docker_descriptor_path,
                       std::string const& docker_mount_substitute,
#endif
                       boost::optional<std::string> const& log_level,
                       boost::optional<std::string> const& log_path)
    {
      ELLE_TRACE_SCOPE("start");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      _run(ifnt, cli,
           login_user,
           default_network,
           advertise_host,
           fetch,
           push,
#ifdef WITH_DOCKER
           docker,
           docker_user,
           docker_home,
           docker_socket_tcp,
           docker_socket_port,
           docker_socket_path,
           docker_descriptor_path,
           docker_mount_substitute,
#endif
           log_level,
           log_path,
           true);
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
