#pragma once

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

// There is a lot of code duplication in these files because we have a
// huge body of options that go together, e.g., those for
// MountOptions.  A means to factor them between modes is dearly
// needed.

namespace infinit
{
  namespace cli
  {
    class Volume
      : public Object<Volume>
    {
    public:
      using Self = Volume;
      Volume(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::delete_,
                                    cli::export_,
                                    cli::fetch,
                                    cli::import,
                                    cli::list,
                                    cli::mount,
                                    cli::pull,
                                    cli::push,
                                    cli::run,
#if !defined INFINIT_WINDOWS
                                    cli::start,
                                    cli::status,
                                    cli::stop,
#endif
                                    cli::update,
                                    cli::syscall));


      using Strings = std::vector<std::string>;
      template <typename T>
      using Defaulted = elle::Defaulted<T>;

      /*---------.
      | Create.  |
      `---------*/

      MODE(create,
           decltype(cli::name)::Formal<std::string const&>,
           decltype(cli::network)::Formal<std::string const&>,
           decltype(cli::description = boost::optional<std::string>()),
           decltype(cli::create_root = false),
           decltype(cli::push_volume = elle::defaulted(false)),
           decltype(cli::output = boost::optional<std::string>()),
           decltype(cli::default_permissions = boost::optional<std::string>()),
           decltype(cli::register_service = false),
           decltype(cli::allow_root_creation = false),
           decltype(cli::mountpoint = boost::optional<std::string>()),
           decltype(cli::readonly = elle::defaulted(false)),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
           decltype(cli::mount_name = boost::optional<std::string>()),
#endif
#if defined INFINIT_MACOSX
           decltype(cli::mount_icon = boost::optional<std::string>()),
           decltype(cli::finder_sidebar = false),
#endif
           decltype(cli::async = elle::defaulted(false)),
#if ! defined INFINIT_WINDOWS
           decltype(cli::daemon = false),
#endif
           decltype(cli::monitoring = elle::defaulted(true)),
           decltype(cli::fuse_option = elle::defaulted(Strings{})),
           decltype(cli::cache = elle::defaulted(false)),
           decltype(cli::cache_ram_size = boost::optional<int>()),
           decltype(cli::cache_ram_ttl = boost::optional<int>()),
           decltype(cli::cache_ram_invalidation = boost::optional<int>()),
           decltype(cli::cache_disk_size = boost::optional<uint64_t>()),
           decltype(cli::fetch_endpoints = elle::defaulted(false)),
           decltype(cli::fetch = elle::defaulted(false)),
           decltype(cli::peer = elle::defaulted(Strings{})),
           decltype(cli::peers_file = boost::optional<std::string>()),
           decltype(cli::push_endpoints = elle::defaulted(false)),
           decltype(cli::push = elle::defaulted(false)),
           decltype(cli::publish = elle::defaulted(false)),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::endpoints_file = boost::optional<std::string>()),
           decltype(cli::port_file = boost::optional<std::string>()),
           decltype(cli::port = boost::optional<int>()),
           decltype(cli::listen = boost::optional<std::string>()),
           decltype(cli::fetch_endpoints_interval = elle::defaulted(300)),
           decltype(cli::input = boost::optional<std::string>()),
           decltype(cli::block_size = elle::defaulted<int>(1024 * 1024)));
      void
      mode_create(std::string const& name,
                  std::string const& network,
                  boost::optional<std::string> description = {},
                  bool create_root = false,
                  Defaulted<bool> push_volume = false,
                  boost::optional<std::string> output = {},
                  boost::optional<std::string> default_permissions = {},
                  bool register_service = false,
                  bool allow_root_creation = false,
                  boost::optional<std::string> mountpoint = {},
                  Defaulted<bool> readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                  boost::optional<std::string> mount_name = {},
#endif
#ifdef INFINIT_MACOSX
                  boost::optional<std::string> mount_icon = {},
                  bool finder_sidebar = false,
#endif
                  Defaulted<bool> async = false,
#ifndef INFINIT_WINDOWS
                  bool daemon = false,
#endif
                  Defaulted<bool> monitoring = true,
                  Defaulted<Strings> fuse_option = elle::defaulted(Strings{}),
                  Defaulted<bool> cache = false,
                  boost::optional<int> cache_ram_size = {},
                  boost::optional<int> cache_ram_ttl = {},
                  boost::optional<int> cache_ram_invalidation = {},
                  boost::optional<uint64_t> cache_disk_size = {},
                  Defaulted<bool> fetch_endpoints = false,
                  Defaulted<bool> fetch = false,
                  Defaulted<Strings> peer = elle::defaulted(Strings{}),
                  boost::optional<std::string> peers_file = {},
                  Defaulted<bool> push_endpoints = false,
                  Defaulted<bool> push = false,
                  Defaulted<bool> publish = false,
                  Strings advertise_host = {},
                  boost::optional<std::string> endpoints_file = {},
                  boost::optional<std::string> port_file = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> listen = {},
                  Defaulted<int> fetch_endpoints_interval = 300,
                  boost::optional<std::string> input = {},
                  Defaulted<int> block_size = 1024 * 1024);

      /*---------------.
      | Mode: delete.  |
      `---------------*/

      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::pull = false),
                 decltype(cli::purge = false)),
           decltype(modes::mode_delete)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge);


      /*---------------.
      | Mode: export.  |
      `---------------*/

      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::output = boost::optional<std::string>())),
           decltype(modes::mode_export)>
      export_;
      void
      mode_export(std::string const& volume_name,
                  boost::optional<std::string> const& output_name = {});

      /*--------------.
      | Mode: fetch.  |
      `--------------*/

      Mode<Volume,
           void (decltype(cli::name = boost::optional<std::string>()),
                 decltype(cli::network = boost::optional<std::string>()),
                 decltype(cli::service = false)),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> volume_name = {},
                 boost::optional<std::string> network_name = {},
                 bool service = false);

      /*---------------.
      | Mode: import.  |
      `---------------*/

      Mode<Volume,
           void (decltype(cli::input = boost::optional<std::string>()),
                 decltype(cli::mountpoint = boost::optional<std::string>())),
           decltype(modes::mode_import)>
      import;
      void
      mode_import(boost::optional<std::string> input_name = {},
                  boost::optional<std::string> mountpoint_name = {});

      /*-------------.
      | Mode: list.  |
      `-------------*/

      Mode<Volume,
           void (),
           decltype(modes::mode_list)>
      list;
      void
      mode_list();

      /*--------------.
      | Mode: mount.  |
      `--------------*/

      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::allow_root_creation = false),
                 decltype(cli::mountpoint = boost::optional<std::string>()),
                 decltype(cli::readonly = elle::defaulted(false)),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                 decltype(cli::mount_name = boost::optional<std::string>()),
#endif
#ifdef INFINIT_MACOSX
                 decltype(cli::mount_icon = boost::optional<std::string>()),
                 decltype(cli::finder_sidebar = false),
#endif
                 decltype(cli::async = elle::defaulted(false)),
#ifndef INFINIT_WINDOWS
                 decltype(cli::daemon = false),
#endif
                 decltype(cli::monitoring = elle::defaulted(true)),
                 decltype(cli::fuse_option = elle::defaulted(Strings{})),
                 decltype(cli::cache = elle::defaulted(false)),
                 decltype(cli::cache_ram_size = boost::optional<int>()),
                 decltype(cli::cache_ram_ttl = boost::optional<int>()),
                 decltype(cli::cache_ram_invalidation = boost::optional<int>()),
                 decltype(cli::cache_disk_size = boost::optional<uint64_t>()),
                 decltype(cli::fetch_endpoints = elle::defaulted(false)),
                 decltype(cli::fetch = elle::defaulted(false)),
                 decltype(cli::peer = elle::defaulted(Strings{})),
                 decltype(cli::peers_file = boost::optional<std::string>()),
                 decltype(cli::push_endpoints = elle::defaulted(false)),
                 decltype(cli::register_service = false),
                 decltype(cli::no_local_endpoints = false),
                 decltype(cli::no_public_endpoints = false),
                 decltype(cli::push = elle::defaulted(false)),
                 decltype(cli::map_other_permissions = true),
                 decltype(cli::publish = elle::defaulted(false)),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::endpoints_file = boost::optional<std::string>()),
                 decltype(cli::port_file = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::listen = boost::optional<std::string>()),
                 decltype(cli::fetch_endpoints_interval = elle::defaulted(300)),
                 decltype(cli::input = boost::optional<std::string>()),
                 decltype(cli::disable_UTF_8_conversion = false),
                 decltype(cli::grpc = boost::optional<std::string>())
#if INFINIT_ENABLE_PROMETHEUS
                 , decltype(cli::prometheus = boost::optional<std::string>())
#endif
                 ),
           decltype(modes::mode_mount)>
      mount;
      void
      mode_mount(std::string const& name,
                 bool allow_root_creation = false,
                 boost::optional<std::string> mountpoint = {},
                 Defaulted<bool> readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                 boost::optional<std::string> mount_name = {},
#endif
#ifdef INFINIT_MACOSX
                 boost::optional<std::string> mount_icon = {},
                 bool finder_sidebar = false,
#endif
                 Defaulted<bool> async = false,
#ifndef INFINIT_WINDOWS
                 bool daemon = false,
#endif
                 Defaulted<bool> monitoring = true,
                 Defaulted<Strings> fuse_option = elle::defaulted(Strings{}),
                 Defaulted<bool> cache = false,
                 boost::optional<int> cache_ram_size = {},
                 boost::optional<int> cache_ram_ttl = {},
                 boost::optional<int> cache_ram_invalidation = {},
                 boost::optional<uint64_t> cache_disk_size = {},
                 Defaulted<bool> fetch_endpoints = false,
                 Defaulted<bool> fetch = false,
                 Defaulted<Strings> peer = elle::defaulted(Strings{}),
                 boost::optional<std::string> peers_file = {},
                 Defaulted<bool> push_endpoints = false,
                 bool register_service = false,
                 bool no_local_endpoints = false,
                 bool no_public_endpoints = false,
                 Defaulted<bool> push = false,
                 bool map_other_permissions = true,
                 Defaulted<bool> publish = false,
                 Strings advertise_host = {},
                 boost::optional<std::string> endpoints_file = {},
                 boost::optional<std::string> port_file = {},
                 boost::optional<int> port = {},
                 boost::optional<std::string> listen = {},
                 Defaulted<int> fetch_endpoints_interval = 300,
                 boost::optional<std::string> input = {},
                 bool disable_UTF_8_conversion = false,
                 boost::optional<std::string> grpc = boost::none
#if INFINIT_ENABLE_PROMETHEUS
                 , boost::optional<std::string> prometheus = {}
#endif
                 );

      /*-------------.
      | Mode: pull.  |
      `-------------*/

      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::purge = false)),
           decltype(modes::mode_pull)>
      pull;
      void
      mode_pull(std::string const& name,
                bool purge = false);

      /*-------------.
      | Mode: push.  |
      `-------------*/

      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_push)>
      push;
      void
      mode_push(std::string const& name);

      /*------------.
      | Mode: run.  |
      `------------*/

      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::allow_root_creation = false),
                 decltype(cli::mountpoint = boost::optional<std::string>()),
                 decltype(cli::readonly = elle::defaulted(false)),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                 decltype(cli::mount_name = boost::optional<std::string>()),
#endif
#ifdef INFINIT_MACOSX
                 decltype(cli::mount_icon = boost::optional<std::string>()),
                 decltype(cli::finder_sidebar = false),
#endif
                 decltype(cli::async = elle::defaulted(false)),
#ifndef INFINIT_WINDOWS
                 decltype(cli::daemon = false),
#endif
                 decltype(cli::monitoring = elle::defaulted(true)),
                 decltype(cli::fuse_option = elle::defaulted(Strings{})),
                 decltype(cli::cache = elle::defaulted(false)),
                 decltype(cli::cache_ram_size = boost::optional<int>()),
                 decltype(cli::cache_ram_ttl = boost::optional<int>()),
                 decltype(cli::cache_ram_invalidation = boost::optional<int>()),
                 decltype(cli::cache_disk_size = boost::optional<uint64_t>()),
                 decltype(cli::fetch_endpoints = elle::defaulted(false)),
                 decltype(cli::fetch = elle::defaulted(false)),
                 decltype(cli::peer = elle::defaulted(Strings{})),
                 decltype(cli::peers_file = boost::optional<std::string>()),
                 decltype(cli::push_endpoints = elle::defaulted(false)),
                 decltype(cli::register_service = false),
                 decltype(cli::no_local_endpoints = false),
                 decltype(cli::no_public_endpoints = false),
                 decltype(cli::push = elle::defaulted(false)),
                 decltype(cli::map_other_permissions = true),
                 decltype(cli::publish = elle::defaulted(false)),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::endpoints_file = boost::optional<std::string>()),
                 decltype(cli::port_file = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::listen = boost::optional<std::string>()),
                 decltype(cli::fetch_endpoints_interval = elle::defaulted(300)),
                 decltype(cli::input = boost::optional<std::string>()),
                 decltype(cli::disable_UTF_8_conversion = false),
                 decltype(cli::grpc = boost::optional<std::string>())
#if INFINIT_ENABLE_PROMETHEUS
                 , decltype(cli::prometheus = boost::optional<std::string>())
#endif
                 ),
           decltype(modes::mode_run)>
      run;
      void
      mode_run(std::string const& name,
               bool allow_root_creation = false,
               boost::optional<std::string> mountpoint = {},
               Defaulted<bool> readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
               boost::optional<std::string> mount_name = {},
#endif
#ifdef INFINIT_MACOSX
               boost::optional<std::string> mount_icon = {},
               bool finder_sidebar = false,
#endif
               Defaulted<bool> async = false,
#ifndef INFINIT_WINDOWS
               bool daemon = false,
#endif
               Defaulted<bool> monitoring = true,
               Defaulted<Strings> fuse_option = elle::defaulted(Strings{}),
               Defaulted<bool> cache = false,
               boost::optional<int> cache_ram_size = {},
               boost::optional<int> cache_ram_ttl = {},
               boost::optional<int> cache_ram_invalidation = {},
               boost::optional<uint64_t> cache_disk_size = {},
               Defaulted<bool> fetch_endpoints = false,
               Defaulted<bool> fetch = false,
               Defaulted<Strings> peer = elle::defaulted(Strings{}),
               boost::optional<std::string> peers_file = {},
               Defaulted<bool> push_endpoints = false,
               bool register_service = false,
               bool no_local_endpoints = false,
               bool no_public_endpoints = false,
               Defaulted<bool> push = false,
               bool map_other_permissions = true,
               Defaulted<bool> publish = false,
               Strings advertise_host = {},
               boost::optional<std::string> endpoints_file = {},
               boost::optional<std::string> port_file = {},
               boost::optional<int> port = {},
               boost::optional<std::string> listen = {},
               Defaulted<int> fetch_endpoints_interval = 300,
               boost::optional<std::string> input = {},
               bool disable_UTF_8_conversion = false,
               boost::optional<std::string> grpc = {}
#if INFINIT_ENABLE_PROMETHEUS
               , boost::optional<std::string> prometheus = {}
#endif
               );

      /*--------------.
      | Mode: start.  |
      `--------------*/

#if !defined INFINIT_WINDOWS
      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::allow_root_creation = false),
                 decltype(cli::mountpoint = boost::optional<std::string>()),
                 decltype(cli::readonly = elle::defaulted(false)),
# if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                 decltype(cli::mount_name = boost::optional<std::string>()),
# endif
# ifdef INFINIT_MACOSX
                 decltype(cli::mount_icon = boost::optional<std::string>()),
                 decltype(cli::finder_sidebar = false),
# endif
                 decltype(cli::async = elle::defaulted(false)),
# ifndef INFINIT_WINDOWS
                 decltype(cli::daemon = false),
# endif
                 decltype(cli::monitoring = elle::defaulted(true)),
                 decltype(cli::fuse_option = elle::defaulted(Strings{})),
                 decltype(cli::cache = elle::defaulted(false)),
                 decltype(cli::cache_ram_size = boost::optional<int>()),
                 decltype(cli::cache_ram_ttl = boost::optional<int>()),
                 decltype(cli::cache_ram_invalidation = boost::optional<int>()),
                 decltype(cli::cache_disk_size = boost::optional<uint64_t>()),
                 decltype(cli::fetch_endpoints = elle::defaulted(false)),
                 decltype(cli::fetch = elle::defaulted(false)),
                 decltype(cli::peer = elle::defaulted(Strings{})),
                 decltype(cli::peers_file = boost::optional<std::string>()),
                 decltype(cli::push_endpoints = elle::defaulted(false)),
                 decltype(cli::register_service = false),
                 decltype(cli::no_local_endpoints = false),
                 decltype(cli::no_public_endpoints = false),
                 decltype(cli::push = elle::defaulted(false)),
                 decltype(cli::map_other_permissions = true),
                 decltype(cli::publish = elle::defaulted(false)),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::endpoints_file = boost::optional<std::string>()),
                 decltype(cli::port_file = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::listen = boost::optional<std::string>()),
                 decltype(cli::fetch_endpoints_interval = elle::defaulted(300)),
                 decltype(cli::input = boost::optional<std::string>())),
           decltype(modes::mode_start)>
      start;
      void
      mode_start(std::string const& name,
                 bool allow_root_creation = false,
                 boost::optional<std::string> mountpoint = {},
                 Defaulted<bool> readonly = false,
# if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                 boost::optional<std::string> mount_name = {},
# endif
# ifdef INFINIT_MACOSX
                 boost::optional<std::string> mount_icon = {},
                 bool finder_sidebar = false,
# endif
                 Defaulted<bool> async = false,
# ifndef INFINIT_WINDOWS
                 bool daemon = false,
# endif
                 Defaulted<bool> monitoring = true,
                 Defaulted<Strings> fuse_option = elle::defaulted(Strings{}),
                 Defaulted<bool> cache = false,
                 boost::optional<int> cache_ram_size = {},
                 boost::optional<int> cache_ram_ttl = {},
                 boost::optional<int> cache_ram_invalidation = {},
                 boost::optional<uint64_t> cache_disk_size = {},
                 Defaulted<bool> fetch_endpoints = false,
                 Defaulted<bool> fetch = false,
                 Defaulted<Strings> peer = elle::defaulted(Strings{}),
                 boost::optional<std::string> peers_file = {},
                 Defaulted<bool> push_endpoints = false,
                 bool register_service = false,
                 bool no_local_endpoints = false,
                 bool no_public_endpoints = false,
                 Defaulted<bool> push = false,
                 bool map_other_permissions = true,
                 Defaulted<bool> publish = false,
                 Strings advertise_host = {},
                 boost::optional<std::string> endpoints_file = {},
                 boost::optional<std::string> port_file = {},
                 boost::optional<int> port = {},
                 boost::optional<std::string> listen = {},
                 Defaulted<int> fetch_endpoints_interval = 300,
                 boost::optional<std::string> input = {});


      /*---------------.
      | Mode: status.  |
      `---------------*/
      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_status)>
      status;
      void
      mode_status(std::string const& volume_name);

      /*-------------.
      | Mode: stop.  |
      `-------------*/
      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_stop)>
      stop;
      void
      mode_stop(std::string const& volume_name);
#endif

      /*---------------.
      | Mode: update.  |
      `---------------*/
      Mode<Volume,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::allow_root_creation = false),
                 decltype(cli::mountpoint = boost::optional<std::string>()),
                 decltype(cli::readonly = elle::defaulted(false)),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                 decltype(cli::mount_name = boost::optional<std::string>()),
#endif
#ifdef INFINIT_MACOSX
                 decltype(cli::mount_icon = boost::optional<std::string>()),
                 decltype(cli::finder_sidebar = false),
#endif
                 decltype(cli::async = elle::defaulted(false)),
#ifndef INFINIT_WINDOWS
                 decltype(cli::daemon = false),
#endif
                 decltype(cli::monitoring = elle::defaulted(true)),
                 decltype(cli::fuse_option = elle::defaulted(Strings{})),
                 decltype(cli::cache = elle::defaulted(false)),
                 decltype(cli::cache_ram_size = boost::optional<int>()),
                 decltype(cli::cache_ram_ttl = boost::optional<int>()),
                 decltype(cli::cache_ram_invalidation = boost::optional<int>()),
                 decltype(cli::cache_disk_size = boost::optional<uint64_t>()),
                 decltype(cli::fetch_endpoints = elle::defaulted(false)),
                 decltype(cli::fetch = elle::defaulted(false)),
                 decltype(cli::peer = elle::defaulted(Strings{})),
                 decltype(cli::peers_file = boost::optional<std::string>()),
                 decltype(cli::push_endpoints = elle::defaulted(false)),
                 decltype(cli::push = elle::defaulted(false)),
                 decltype(cli::map_other_permissions = true),
                 decltype(cli::publish = elle::defaulted(false)),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::endpoints_file = boost::optional<std::string>()),
                 decltype(cli::port_file = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::listen = boost::optional<std::string>()),
                 decltype(cli::fetch_endpoints_interval = elle::defaulted(300)),
                 decltype(cli::input = boost::optional<std::string>()),
                 decltype(cli::user = boost::optional<std::string>()),
                 decltype(cli::block_size = boost::optional<int>())),
           decltype(modes::mode_update)>
      update;
      void
      mode_update(std::string const& name,
                  boost::optional<std::string> description = {},
                  bool allow_root_creation = false,
                  boost::optional<std::string> mountpoint = {},
                  Defaulted<bool> readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                  boost::optional<std::string> mount_name = {},
#endif
#ifdef INFINIT_MACOSX
                  boost::optional<std::string> mount_icon = {},
                  bool finder_sidebar = false,
#endif
                  Defaulted<bool> async = false,
#ifndef INFINIT_WINDOWS
                  bool daemon = false,
#endif
                  Defaulted<bool> monitoring = true,
                  Defaulted<Strings> fuse_option = Strings{},
                  Defaulted<bool> cache = false,
                  boost::optional<int> cache_ram_size = {},
                  boost::optional<int> cache_ram_ttl = {},
                  boost::optional<int> cache_ram_invalidation = {},
                  boost::optional<uint64_t> cache_disk_size = {},
                  Defaulted<bool> fetch_endpoints = false,
                  Defaulted<bool> fetch = false,
                  Defaulted<Strings> peer = Strings{},
                  boost::optional<std::string> peers_file = {},
                  Defaulted<bool> push_endpoints = false,
                  Defaulted<bool> push = false,
                  bool map_other_permissions = true,
                  Defaulted<bool> publish = false,
                  Strings advertise_host = {},
                  boost::optional<std::string> endpoints_file = {},
                  boost::optional<std::string> port_file = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> listen = {},
                  Defaulted<int> fetch_endpoints_interval = 300,
                  boost::optional<std::string> input = {},
                  boost::optional<std::string> user = {},
                  boost::optional<int> block_size = {});

            using Objects = decltype(elle::meta::list(cli::syscall));

      // Syscall.
      class Syscall
        : public Object<Syscall, Volume>
      {
      public:
        using Super = Object<Syscall, Volume>;
        using Modes = decltype(elle::meta::list(cli::getxattr,
                                                cli::setxattr));
        Syscall(Infinit& infinit);
        // getxattr.
        Mode<Syscall,
             void (decltype(cli::path)::Formal<std::string const&>,
                   decltype(cli::name)::Formal<std::string const&>),
             decltype(modes::mode_get_xattr) >
        getxattr;
        void
        mode_get_xattr(std::string const& path,
                       std::string const& name);

        // setxattr.
        Mode<Syscall,
             void (decltype(cli::path)::Formal<std::string const&>,
                   decltype(cli::name)::Formal<std::string const&>,
                   decltype(cli::value)::Formal<std::string const&>),
             decltype(modes::mode_set_xattr)>
        setxattr;
        void
        mode_set_xattr(std::string const& path,
                       std::string const& attribute,
                       std::string const& value);

        std::string const description = "Syscalls";
      } syscall;
    };
  }
}
