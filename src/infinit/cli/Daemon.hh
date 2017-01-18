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
                              cli::mount_root,
                              cli::default_network,
                              cli::advertise_host,
                              cli::fetch,
                              cli::push,
#ifdef WITH_DOCKER
                              cli::docker,
                              cli::docker_user,
                              cli::docker_home,
                              cli::docker_socket_tcp,
                              cli::docker_socket_port,
                              cli::docker_socket_path,
                              cli::docker_descriptor_path,
                              cli::docker_mount_substitute,
#endif
                              cli::log_level,
                              cli::log_path))>;
      ModeRun run;
      void
      mode_run(Strings const& login_user,
               Strings const& mount,
               boost::optional<std::string> const& mount_root,
               boost::optional<std::string> const& default_network,
               boost::optional<Strings> const& advertise_host,
               bool fetch,
               bool push,
#ifdef WITH_DOCKER
               bool docker,
               boost::optional<std::string> const& docker_user,
               boost::optional<std::string> const& docker_home,
               bool docker_socket_tcp,
               boost::optional<int> const& docker_socket_port,
               boost::optional<std::string> const& docker_socket_path,
               boost::optional<std::string> const& docker_descriptor_path,
               boost::optional<std::string> const& docker_mount_substitute,
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
                              cli::mount_root,
                              cli::default_network,
                              cli::advertise_host,
                              cli::fetch,
                              cli::push,
#ifdef WITH_DOCKER
                              cli::docker,
                              cli::docker_user,
                              cli::docker_home,
                              cli::docker_socket_tcp,
                              cli::docker_socket_port,
                              cli::docker_socket_path,
                              cli::docker_descriptor_path,
                              cli::docker_mount_substitute,
#endif
                              cli::log_level,
                              cli::log_path))>;
      ModeStart start;
      void
      mode_start(Strings const& login_user,
                 Strings const& mount,
                 boost::optional<std::string> const& mount_root,
                 boost::optional<std::string> const& default_network,
                 boost::optional<Strings> const& advertise_host,
                 bool fetch,
                 bool push,
#ifdef WITH_DOCKER
                 bool docker,
                 boost::optional<std::string> const& docker_user,
                 boost::optional<std::string> const& docker_home,
                 bool docker_socket_tcp,
                 boost::optional<int> const& docker_socket_port,
                 boost::optional<std::string> const& docker_socket_path,
                 boost::optional<std::string> const& docker_descriptor_path,
                 boost::optional<std::string> const& docker_mount_substitute,
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
