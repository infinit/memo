#pragma once

#include <das/cli.hh>

#include <infinit/cli/Entity.hh>
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
      : public Entity<Volume>
    {
    public:
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
#if !defined INFINIT_WINDOWS
                                    cli::start,
                                    cli::status,
                                    cli::stop,
#endif
                                    cli::run));

      using Strings = std::vector<std::string>;
      template <typename T>
      using Defaulted = das::cli::Defaulted<T>;

      /*---------.
      | Create.  |
      `---------*/
      Mode<decltype(binding(modes::mode_create,
                            cli::name,
                            cli::network,
                            cli::description = boost::none,
                            cli::create_root = false,
                            cli::push_volume = false,
                            cli::output = boost::none,
                            cli::default_permissions = boost::none,
                            cli::register_service = false,
                            cli::allow_root_creation = false,
                            cli::mountpoint = boost::none,
                            cli::readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                            cli::mount_name = boost::none,
#endif
#ifdef INFINIT_MACOSX
                            cli::mount_icon = boost::none,
                            cli::finder_sidebar = false,
#endif
                            cli::async = false,
#ifndef INFINIT_WINDOWS
                            cli::daemon = false,
#endif
                            cli::monitoring = true,
                            cli::fuse_option = Strings{},
                            cli::cache = false,
                            cli::cache_ram_size = boost::none,
                            cli::cache_ram_ttl = boost::none,
                            cli::cache_ram_invalidation = boost::none,
                            cli::cache_disk_size = boost::none,
                            cli::fetch_endpoints = false,
                            cli::fetch = false,
                            cli::peer = Strings{},
                            cli::peers_file = boost::none,
                            cli::push_endpoints = false,
                            cli::push = false,
                            cli::publish = false,
                            cli::advertise_host = Strings{},
                            cli::endpoints_file = boost::none,
                            cli::port_file = boost::none,
                            cli::port = boost::none,
                            cli::listen = boost::none,
                            cli::fetch_endpoints_interval = 300,
                            cli::input = boost::none
                            ))>
      create;
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
                  Defaulted<Strings> fuse_option = Strings{},
                  Defaulted<bool> cache = false,
                  boost::optional<int> cache_ram_size = {},
                  boost::optional<int> cache_ram_ttl = {},
                  boost::optional<int> cache_ram_invalidation = {},
                  boost::optional<int> cache_disk_size = {},
                  Defaulted<bool> fetch_endpoints = false,
                  Defaulted<bool> fetch = false,
                  Defaulted<Strings> peer = Strings{},
                  boost::optional<std::string> peers_file = {},
                  Defaulted<bool> push_endpoints = false,
                  Defaulted<bool> push = false,
                  bool publish = false,
                  Strings advertise_host = {},
                  boost::optional<std::string> endpoints_file = {},
                  boost::optional<std::string> port_file = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> listen = {},
                  Defaulted<int> fetch_endpoints_interval = 300,
                  boost::optional<std::string> input = {});


      /*---------------.
      | Mode: delete.  |
      `---------------*/
      using ModeDelete =
        Mode<decltype(binding(modes::mode_delete,
                              cli::name,
                              cli::pull = false,
                              cli::purge = false))>;
      ModeDelete delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge);


      /*---------------.
      | Mode: export.  |
      `---------------*/
      using ModeExport =
        Mode<decltype(binding(modes::mode_export,
                              cli::name,
                              cli::output = boost::none))>;
      ModeExport export_;
      void
      mode_export(std::string const& volume_name,
                  boost::optional<std::string> const& output_name = {});


      /*--------------.
      | Mode: fetch.  |
      `--------------*/
      using ModeFetch =
        Mode<decltype(binding(modes::mode_fetch,
                              cli::name = boost::none,
                              cli::network = boost::none,
                              cli::service = false))>;
      ModeFetch fetch;
      void
      mode_fetch(boost::optional<std::string> volume_name = {},
                 boost::optional<std::string> network_name = {},
                 bool service = false);

      /*---------------.
      | Mode: import.  |
      `---------------*/
      using ModeImport =
        Mode<decltype(binding(modes::mode_import,
                              cli::input = boost::none,
                              cli::mountpoint = boost::none))>;
      ModeImport import;
      void
      mode_import(boost::optional<std::string> input_name = {},
                  boost::optional<std::string> mountpoint_name = {});


      /*-------------.
      | Mode: list.  |
      `-------------*/
      using ModeList =
        Mode<decltype(binding(modes::mode_list))>;
      ModeList list;
      void
      mode_list();


      /*--------------.
      | Mode: mount.  |
      `--------------*/
      Mode<decltype(binding(modes::mode_mount,
                            cli::name,
                            cli::allow_root_creation = false,
                            cli::mountpoint = boost::none,
                            cli::readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                            cli::mount_name = boost::none,
#endif
#ifdef INFINIT_MACOSX
                            cli::mount_icon = boost::none,
                            cli::finder_sidebar = false,
#endif
                            cli::async = false,
#ifndef INFINIT_WINDOWS
                            cli::daemon = false,
#endif
                            cli::monitoring = true,
                            cli::fuse_option = Strings{},
                            cli::cache = false,
                            cli::cache_ram_size = boost::none,
                            cli::cache_ram_ttl = boost::none,
                            cli::cache_ram_invalidation = boost::none,
                            cli::cache_disk_size = boost::none,
                            cli::fetch_endpoints = false,
                            cli::fetch = false,
                            cli::peer = Strings{},
                            cli::peers_file = boost::none,
                            cli::push_endpoints = false,
                            cli::register_service = false,
                            cli::no_local_endpoints = false,
                            cli::no_public_endpoints = false,
                            cli::push = false,
                            cli::map_other_permissions = true,
                            cli::publish = false,
                            cli::advertise_host = Strings{},
                            cli::endpoints_file = boost::none,
                            cli::port_file = boost::none,
                            cli::port = boost::none,
                            cli::listen = boost::none,
                            cli::fetch_endpoints_interval = 300,
                            cli::input = boost::none,
                            cli::disable_UTF_8_conversion = false))>
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
                 Defaulted<Strings> fuse_option = Strings{},
                 Defaulted<bool> cache = false,
                 boost::optional<int> cache_ram_size = {},
                 boost::optional<int> cache_ram_ttl = {},
                 boost::optional<int> cache_ram_invalidation = {},
                 boost::optional<int> cache_disk_size = {},
                 Defaulted<bool> fetch_endpoints = false,
                 Defaulted<bool> fetch = false,
                 Defaulted<Strings> peer = Strings{},
                 boost::optional<std::string> peers_file = {},
                 Defaulted<bool> push_endpoints = false,
                 bool register_service = false,
                 bool no_local_endpoints = false,
                 bool no_public_endpoints = false,
                 Defaulted<bool> push = false,
                 bool map_other_permissions = true,
                 bool publish = false,
                 Strings advertise_host = {},
                 boost::optional<std::string> endpoints_file = {},
                 boost::optional<std::string> port_file = {},
                 boost::optional<int> port = {},
                 boost::optional<std::string> listen = {},
                 Defaulted<int> fetch_endpoints_interval = 300,
                 boost::optional<std::string> input = {},
                 bool disable_UTF_8_conversion = false);


      /*-------------.
      | Mode: pull.  |
      `-------------*/
      using ModePull =
        Mode<decltype(binding(modes::mode_pull,
                              cli::name,
                              cli::purge = false))>;
      ModePull pull;
      void
      mode_pull(std::string const& name,
                bool purge = false);


      /*-------------.
      | Mode: push.  |
      `-------------*/
      using ModePush =
        Mode<decltype(binding(modes::mode_push,
                              cli::name))>;
      ModePush push;
      void
      mode_push(std::string const& name);


      /*------------.
      | Mode: run.  |
      `------------*/
      Mode<decltype(binding(modes::mode_run,
                            cli::name,
                            cli::allow_root_creation = false,
                            cli::mountpoint = boost::none,
                            cli::readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                            cli::mount_name = boost::none,
#endif
#ifdef INFINIT_MACOSX
                            cli::mount_icon = boost::none,
                            cli::finder_sidebar = false,
#endif
                            cli::async = false,
#ifndef INFINIT_WINDOWS
                            cli::daemon = false,
#endif
                            cli::monitoring = true,
                            cli::fuse_option = Strings{},
                            cli::cache = false,
                            cli::cache_ram_size = boost::none,
                            cli::cache_ram_ttl = boost::none,
                            cli::cache_ram_invalidation = boost::none,
                            cli::cache_disk_size = boost::none,
                            cli::fetch_endpoints = false,
                            cli::fetch = false,
                            cli::peer = Strings{},
                            cli::peers_file = boost::none,
                            cli::push_endpoints = false,
                            cli::register_service = false,
                            cli::no_local_endpoints = false,
                            cli::no_public_endpoints = false,
                            cli::push = false,
                            cli::map_other_permissions = true,
                            cli::publish = false,
                            cli::advertise_host = Strings{},
                            cli::endpoints_file = boost::none,
                            cli::port_file = boost::none,
                            cli::port = boost::none,
                            cli::listen = boost::none,
                            cli::fetch_endpoints_interval = 300,
                            cli::input = boost::none,
                            cli::disable_UTF_8_conversion = false))>
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
               Defaulted<Strings> fuse_option = Strings{},
               Defaulted<bool> cache = false,
               boost::optional<int> cache_ram_size = {},
               boost::optional<int> cache_ram_ttl = {},
               boost::optional<int> cache_ram_invalidation = {},
               boost::optional<int> cache_disk_size = {},
               Defaulted<bool> fetch_endpoints = false,
               Defaulted<bool> fetch = false,
               Defaulted<Strings> peer = Strings{},
               boost::optional<std::string> peers_file = {},
               Defaulted<bool> push_endpoints = false,
               bool register_service = false,
               bool no_local_endpoints = false,
               bool no_public_endpoints = false,
               Defaulted<bool> push = false,
               bool map_other_permissions = true,
               bool publish = false,
               Strings advertise_host = {},
               boost::optional<std::string> endpoints_file = {},
               boost::optional<std::string> port_file = {},
               boost::optional<int> port = {},
               boost::optional<std::string> listen = {},
               Defaulted<int> fetch_endpoints_interval = 300,
               boost::optional<std::string> input = {},
               bool disable_UTF_8_conversion = false);


      /*--------------.
      | Mode: start.  |
      `--------------*/
#if !defined INFINIT_WINDOWS
      Mode<decltype(binding(modes::mode_start,
                            cli::name,
                            cli::allow_root_creation = false,
                            cli::mountpoint = boost::none,
                            cli::readonly = false,
# if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                            cli::mount_name = boost::none,
# endif
# ifdef INFINIT_MACOSX
                            cli::mount_icon = boost::none,
                            cli::finder_sidebar = false,
# endif
                            cli::async = false,
# ifndef INFINIT_WINDOWS
                            cli::daemon = false,
# endif
                            cli::monitoring = true,
                            cli::fuse_option = Strings{},
                            cli::cache = false,
                            cli::cache_ram_size = boost::none,
                            cli::cache_ram_ttl = boost::none,
                            cli::cache_ram_invalidation = boost::none,
                            cli::cache_disk_size = boost::none,
                            cli::fetch_endpoints = false,
                            cli::fetch = false,
                            cli::peer = Strings{},
                            cli::peers_file = boost::none,
                            cli::push_endpoints = false,
                            cli::register_service = false,
                            cli::no_local_endpoints = false,
                            cli::no_public_endpoints = false,
                            cli::push = false,
                            cli::map_other_permissions = true,
                            cli::publish = false,
                            cli::advertise_host = Strings{},
                            cli::endpoints_file = boost::none,
                            cli::port_file = boost::none,
                            cli::port = boost::none,
                            cli::listen = boost::none,
                            cli::fetch_endpoints_interval = 300,
                            cli::input = boost::none))>
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
                 Defaulted<Strings> fuse_option = Strings{},
                 Defaulted<bool> cache = false,
                 boost::optional<int> cache_ram_size = {},
                 boost::optional<int> cache_ram_ttl = {},
                 boost::optional<int> cache_ram_invalidation = {},
                 boost::optional<int> cache_disk_size = {},
                 Defaulted<bool> fetch_endpoints = false,
                 Defaulted<bool> fetch = false,
                 Defaulted<Strings> peer = Strings{},
                 boost::optional<std::string> peers_file = {},
                 Defaulted<bool> push_endpoints = false,
                 bool register_service = false,
                 bool no_local_endpoints = false,
                 bool no_public_endpoints = false,
                 Defaulted<bool> push = false,
                 bool map_other_permissions = true,
                 bool publish = false,
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
      using ModeStatus =
        Mode<decltype(binding(modes::mode_status,
                              cli::name))>;
      ModeStatus status;
      void
      mode_status(std::string const& volume_name);


      /*-------------.
      | Mode: stop.  |
      `-------------*/
      using ModeStop =
        Mode<decltype(binding(modes::mode_stop,
                              cli::name))>;
      ModeStop stop;
      void
      mode_stop(std::string const& volume_name);
#endif

      /*---------------.
      | Mode: update.  |
      `---------------*/
      Mode<decltype(binding(modes::mode_update,
                            cli::name,
                            cli::description = boost::none,
                            cli::allow_root_creation = false,
                            cli::mountpoint = boost::none,
                            cli::readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                            cli::mount_name = boost::none,
#endif
#ifdef INFINIT_MACOSX
                            cli::mount_icon = boost::none,
                            cli::finder_sidebar = false,
#endif
                            cli::async = false,
#ifndef INFINIT_WINDOWS
                            cli::daemon = false,
#endif
                            cli::monitoring = true,
                            cli::fuse_option = Strings{},
                            cli::cache = false,
                            cli::cache_ram_size = boost::none,
                            cli::cache_ram_ttl = boost::none,
                            cli::cache_ram_invalidation = boost::none,
                            cli::cache_disk_size = boost::none,
                            cli::fetch_endpoints = false,
                            cli::fetch = false,
                            cli::peer = Strings{},
                            cli::peers_file = boost::none,
                            cli::push_endpoints = false,
                            cli::push = false,
                            cli::map_other_permissions = true,
                            cli::publish = false,
                            cli::advertise_host = Strings{},
                            cli::endpoints_file = boost::none,
                            cli::port_file = boost::none,
                            cli::port = boost::none,
                            cli::listen = boost::none,
                            cli::fetch_endpoints_interval = 300,
                            cli::input = boost::none,
                            cli::user = boost::none))>
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
                  boost::optional<int> cache_disk_size = {},
                  Defaulted<bool> fetch_endpoints = false,
                  Defaulted<bool> fetch = false,
                  Defaulted<Strings> peer = Strings{},
                  boost::optional<std::string> peers_file = {},
                  Defaulted<bool> push_endpoints = false,
                  Defaulted<bool> push = false,
                  bool map_other_permissions = true,
                  bool publish = false,
                  Strings advertise_host = {},
                  boost::optional<std::string> endpoints_file = {},
                  boost::optional<std::string> port_file = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> listen = {},
                  Defaulted<int> fetch_endpoints_interval = 300,
                  boost::optional<std::string> input = {},
                  boost::optional<std::string> user = {});
    };
  }
}
