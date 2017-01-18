#pragma once

#include <das/cli.hh>

#include <infinit/cli/Entity.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Daemon
      : public Entity<Daemon>
    {
    public:
      Daemon(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::run,
                                    cli::start,
                                    cli::status,
                                    cli::stop));
      using Strings = std::vector<std::string>;

      // Whether to enable Docker support.
#if !defined INFINIT_PRODUCTION_BUILD || defined INFINIT_LINUX
# define WITH_DOCKER
#endif

      /*------------.
      | Mode: run.  |
      `------------*/
      using ModeRun =
        Mode<decltype(binding(modes::mode_run,
                              cli::login_user = Strings{},
                              cli::mount = Strings{},
                              cli::mount_root = boost::none,
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
                              cli::log_path = boost::none))>;
      ModeRun run;
      void
      mode_run(Strings const& login_user,
               Strings const& mount,
               boost::optional<std::string> const& mount_root,
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
               boost::optional<std::string> const& log_path);

      /*--------------.
      | Mode: start.  |
      `--------------*/
      using ModeStart =
        Mode<decltype(binding(modes::mode_start,
                              cli::login_user = Strings{},
                              cli::mount = Strings{},
                              cli::mount_root = boost::none,
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
                              cli::log_path = boost::none))>;
      ModeStart start;
      void
      mode_start(Strings const& login_user,
                 Strings const& mount,
                 boost::optional<std::string> const& mount_root,
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
                 boost::optional<std::string> const& log_path);


      /*---------------.
      | Mode: status.  |
      `---------------*/
      using ModeStatus =
        Mode<decltype(binding(modes::mode_status))>;
      ModeStatus status;
      void
      mode_status();

      /*-------------.
      | Mode: stop.  |
      `-------------*/
      using ModeStop =
        Mode<decltype(binding(modes::mode_stop))>;
      ModeStop stop;
      void
      mode_stop();
    };
  }
}
