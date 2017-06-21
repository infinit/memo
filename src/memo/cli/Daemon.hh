#pragma once

#include <elle/das/cli.hh>

#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

/// Whether to enable Docker support.
#if !defined MEMO_PRODUCTION_BUILD || defined INFINIT_LINUX
# define WITH_DOCKER
#endif

namespace memo
{
  namespace cli
  {
    class Daemon
      : public Object<Daemon>
    {
    public:
      Daemon(Memo& memo);
      using Modes
        = decltype(elle::meta::list(cli::disable_storage,
                                    cli::enable_storage,
                                    cli::fetch,
                                    cli::run,
                                    cli::start,
                                    cli::status,
                                    cli::stop));
      using Strings = std::vector<std::string>;

      /*------------------------.
      | Mode: disable_storage.  |
      `------------------------*/
      Mode<Daemon,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_disable_storage)>
      disable_storage;
      void
      mode_disable_storage(std::string const& name);


      /*-----------------------.
      | Mode: enable_storage.  |
      `-----------------------*/
      Mode<Daemon,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::hold)::Formal<bool>),
           decltype(modes::mode_enable_storage)>
      enable_storage;
      void
      mode_enable_storage(std::string const& name,
                          bool hold);


      /*--------------.
      | Mode: fetch.  |
      `--------------*/
      Mode<Daemon,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(std::string const& name);

      /*------------.
      | Mode: run.  |
      `------------*/
      Mode<Daemon,
           void (decltype(cli::login_user = Strings{}),
                 decltype(cli::default_network = boost::optional<std::string>()),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::fetch = false),
                 decltype(cli::push = false),
#ifdef WITH_DOCKER
                 decltype(cli::docker = true),
                 decltype(cli::docker_user = boost::optional<std::string>()),
                 decltype(cli::docker_home = boost::optional<std::string>()),
                 decltype(cli::docker_socket_tcp = false),
                 decltype(cli::docker_socket_port = 0),
                 decltype(cli::docker_socket_path = std::string()),
                 decltype(cli::docker_descriptor_path = std::string()),
                 decltype(cli::docker_mount_substitute = std::string()),
#endif
                 decltype(cli::log_level = boost::optional<std::string>()),
                 decltype(cli::log_path = boost::optional<std::string>())),
           decltype(modes::mode_run)>
      run;
      void
      mode_run(Strings const& login_user,
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
      Mode<Daemon,
           void (decltype(cli::login_user = Strings{}),
                 decltype(cli::default_network = boost::optional<std::string>()),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::fetch = false),
                 decltype(cli::push = false),
#ifdef WITH_DOCKER
                 decltype(cli::docker = true),
                 decltype(cli::docker_user = boost::optional<std::string>()),
                 decltype(cli::docker_home = boost::optional<std::string>()),
                 decltype(cli::docker_socket_tcp = false),
                 decltype(cli::docker_socket_port = 0),
                 decltype(cli::docker_socket_path = std::string()),
                 decltype(cli::docker_descriptor_path = std::string()),
                 decltype(cli::docker_mount_substitute = std::string()),
#endif
                 decltype(cli::log_level = boost::optional<std::string>()),
                 decltype(cli::log_path = boost::optional<std::string>())),
           decltype(modes::mode_start)>
      start;
      void
      mode_start(Strings const& login_user,
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
      Mode<Daemon,
           void (),
           decltype(modes::mode_status)>
      status;
      void
      mode_status();

      /*-------------.
      | Mode: stop.  |
      `-------------*/
      Mode<Daemon,
           void (),
           decltype(modes::mode_stop)>
      stop;
      void
      mode_stop();
    };
  }
}
