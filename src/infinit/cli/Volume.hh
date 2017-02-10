#pragma once

#include <das/cli.hh>

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
                                    cli::update));

      using Strings = std::vector<std::string>;
      template <typename T>
      using Defaulted = elle::Defaulted<T>;

      /*---------.
      | Create.  |
      `---------*/

      Mode<Volume,
           decltype(modes::mode_create),
           decltype(cli::name),
           decltype(cli::network),
           decltype(cli::description = boost::none),
           decltype(cli::create_root = false),
           decltype(cli::push_volume = false),
           decltype(cli::output = boost::none),
           decltype(cli::default_permissions = boost::none),
           decltype(cli::register_service = false),
           decltype(cli::allow_root_creation = false),
           decltype(cli::mountpoint = boost::none),
           decltype(cli::readonly = false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
           decltype(cli::mount_name = boost::none),
#endif
#if defined INFINIT_MACOSX
           decltype(cli::mount_icon = boost::none),
           decltype(cli::finder_sidebar = false),
#endif
           decltype(cli::async = false),
#if ! defined INFINIT_WINDOWS
           decltype(cli::daemon = false),
#endif
           decltype(cli::monitoring = true),
           decltype(cli::fuse_option = Strings{}),
           decltype(cli::cache = false),
           decltype(cli::cache_ram_size = boost::none),
           decltype(cli::cache_ram_ttl = boost::none),
           decltype(cli::cache_ram_invalidation = boost::none),
           decltype(cli::cache_disk_size = boost::none),
           decltype(cli::fetch_endpoints = false),
           decltype(cli::fetch = false),
           decltype(cli::peer = Strings{}),
           decltype(cli::peers_file = boost::none),
           decltype(cli::push_endpoints = false),
           decltype(cli::push = false),
           decltype(cli::publish = false),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::endpoints_file = boost::none),
           decltype(cli::port_file = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::listen = boost::none),
           decltype(cli::fetch_endpoints_interval = 300),
           decltype(cli::input = boost::none),
           decltype(cli::block_size = int())>
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
                  boost::optional<uint64_t> cache_disk_size = {},
                  Defaulted<bool> fetch_endpoints = false,
                  Defaulted<bool> fetch = false,
                  Defaulted<Strings> peer = Strings{},
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
           decltype(modes::mode_delete),
           decltype(cli::name),
           decltype(cli::pull = false),
           decltype(cli::purge = false)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge);


      /*---------------.
      | Mode: export.  |
      `---------------*/

      Mode<Volume,
           decltype(modes::mode_export),
           decltype(cli::name),
           decltype(cli::output = boost::none)>
      export_;
      void
      mode_export(std::string const& volume_name,
                  boost::optional<std::string> const& output_name = {});

      /*--------------.
      | Mode: fetch.  |
      `--------------*/

      Mode<Volume,
           decltype(modes::mode_fetch),
           decltype(cli::name = boost::none),
           decltype(cli::network = boost::none),
           decltype(cli::service = false)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> volume_name = {},
                 boost::optional<std::string> network_name = {},
                 bool service = false);

      /*---------------.
      | Mode: import.  |
      `---------------*/

      Mode<Volume,
           decltype(modes::mode_import),
           decltype(cli::input = boost::none),
           decltype(cli::mountpoint = boost::none)>
      import;
      void
      mode_import(boost::optional<std::string> input_name = {},
                  boost::optional<std::string> mountpoint_name = {});

      /*-------------.
      | Mode: list.  |
      `-------------*/

      Mode<Volume,
           decltype(modes::mode_list)>
      list;
      void
      mode_list();

      /*--------------.
      | Mode: mount.  |
      `--------------*/

      Mode<Volume,
           decltype(modes::mode_mount),
           decltype(cli::name),
           decltype(cli::allow_root_creation = false),
           decltype(cli::mountpoint = boost::none),
           decltype(cli::readonly = false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
           decltype(cli::mount_name = boost::none),
#endif
#ifdef INFINIT_MACOSX
           decltype(cli::mount_icon = boost::none),
           decltype(cli::finder_sidebar = false),
#endif
           decltype(cli::async = false),
#ifndef INFINIT_WINDOWS
           decltype(cli::daemon = false),
#endif
           decltype(cli::monitoring = true),
           decltype(cli::fuse_option = Strings{}),
           decltype(cli::cache = false),
           decltype(cli::cache_ram_size = boost::none),
           decltype(cli::cache_ram_ttl = boost::none),
           decltype(cli::cache_ram_invalidation = boost::none),
           decltype(cli::cache_disk_size = boost::none),
           decltype(cli::fetch_endpoints = false),
           decltype(cli::fetch = false),
           decltype(cli::peer = Strings{}),
           decltype(cli::peers_file = boost::none),
           decltype(cli::push_endpoints = false),
           decltype(cli::register_service = false),
           decltype(cli::no_local_endpoints = false),
           decltype(cli::no_public_endpoints = false),
           decltype(cli::push = false),
           decltype(cli::map_other_permissions = true),
           decltype(cli::publish = false),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::endpoints_file = boost::none),
           decltype(cli::port_file = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::listen = boost::none),
           decltype(cli::fetch_endpoints_interval = 300),
           decltype(cli::input = boost::none),
           decltype(cli::disable_UTF_8_conversion = false)>
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
                 boost::optional<uint64_t> cache_disk_size = {},
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
                 Defaulted<bool> publish = false,
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

      Mode<Volume,
           decltype(modes::mode_pull),
           decltype(cli::name),
           decltype(cli::purge = false)>
      pull;
      void
      mode_pull(std::string const& name,
                bool purge = false);

      /*-------------.
      | Mode: push.  |
      `-------------*/

      Mode<Volume,
           decltype(modes::mode_push),
           decltype(cli::name)>
      push;
      void
      mode_push(std::string const& name);

      /*------------.
      | Mode: run.  |
      `------------*/

      Mode<Volume,
           decltype(modes::mode_run),
           decltype(cli::name),
           decltype(cli::allow_root_creation = false),
           decltype(cli::mountpoint = boost::none),
           decltype(cli::readonly = false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
           decltype(cli::mount_name = boost::none),
#endif
#ifdef INFINIT_MACOSX
           decltype(cli::mount_icon = boost::none),
           decltype(cli::finder_sidebar = false),
#endif
           decltype(cli::async = false),
#ifndef INFINIT_WINDOWS
           decltype(cli::daemon = false),
#endif
           decltype(cli::monitoring = true),
           decltype(cli::fuse_option = Strings{}),
           decltype(cli::cache = false),
        decltype(cli::cache_ram_size = boost::none),
           decltype(cli::cache_ram_ttl = boost::none),
           decltype(cli::cache_ram_invalidation = boost::none),
           decltype(cli::cache_disk_size = boost::none),
           decltype(cli::fetch_endpoints = false),
           decltype(cli::fetch = false),
           decltype(cli::peer = Strings{}),
           decltype(cli::peers_file = boost::none),
           decltype(cli::push_endpoints = false),
           decltype(cli::register_service = false),
           decltype(cli::no_local_endpoints = false),
           decltype(cli::no_public_endpoints = false),
           decltype(cli::push = false),
           decltype(cli::map_other_permissions = true),
           decltype(cli::publish = false),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::endpoints_file = boost::none),
           decltype(cli::port_file = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::listen = boost::none),
           decltype(cli::fetch_endpoints_interval = 300),
           decltype(cli::input = boost::none),
           decltype(cli::disable_UTF_8_conversion = false)>
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
               boost::optional<uint64_t> cache_disk_size = {},
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
               Defaulted<bool> publish = false,
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
      Mode<Volume,
           decltype(modes::mode_start),
           decltype(cli::name),
           decltype(cli::allow_root_creation = false),
           decltype(cli::mountpoint = boost::none),
           decltype(cli::readonly = false),
# if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
           decltype(cli::mount_name = boost::none),
# endif
# ifdef INFINIT_MACOSX
           decltype(cli::mount_icon = boost::none),
           decltype(cli::finder_sidebar = false),
# endif
           decltype(cli::async = false),
# ifndef INFINIT_WINDOWS
           decltype(cli::daemon = false),
# endif
           decltype(cli::monitoring = true),
           decltype(cli::fuse_option = Strings{}),
           decltype(cli::cache = false),
           decltype(cli::cache_ram_size = boost::none),
           decltype(cli::cache_ram_ttl = boost::none),
           decltype(cli::cache_ram_invalidation = boost::none),
           decltype(cli::cache_disk_size = boost::none),
           decltype(cli::fetch_endpoints = false),
           decltype(cli::fetch = false),
           decltype(cli::peer = Strings{}),
           decltype(cli::peers_file = boost::none),
           decltype(cli::push_endpoints = false),
           decltype(cli::register_service = false),
           decltype(cli::no_local_endpoints = false),
           decltype(cli::no_public_endpoints = false),
           decltype(cli::push = false),
           decltype(cli::map_other_permissions = true),
           decltype(cli::publish = false),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::endpoints_file = boost::none),
           decltype(cli::port_file = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::listen = boost::none),
           decltype(cli::fetch_endpoints_interval = 300),
           decltype(cli::input = boost::none)>
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
                 boost::optional<uint64_t> cache_disk_size = {},
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
           decltype(modes::mode_status),
           decltype(cli::name)>
      status;
      void
      mode_status(std::string const& volume_name);

      /*-------------.
      | Mode: stop.  |
      `-------------*/
      Mode<Volume,
           decltype(modes::mode_stop),
           decltype(cli::name)>
      stop;
      void
      mode_stop(std::string const& volume_name);
#endif

      /*---------------.
      | Mode: update.  |
      `---------------*/
      Mode<Volume,
           decltype(modes::mode_update),
           decltype(cli::name),
           decltype(cli::description = boost::none),
           decltype(cli::allow_root_creation = false),
           decltype(cli::mountpoint = boost::none),
           decltype(cli::readonly = false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
           decltype(cli::mount_name = boost::none),
#endif
#ifdef INFINIT_MACOSX
           decltype(cli::mount_icon = boost::none),
           decltype(cli::finder_sidebar = false),
#endif
           decltype(cli::async = false),
#ifndef INFINIT_WINDOWS
        decltype(cli::daemon = false),
#endif
           decltype(cli::monitoring = true),
           decltype(cli::fuse_option = Strings{}),
           decltype(cli::cache = false),
           decltype(cli::cache_ram_size = boost::none),
           decltype(cli::cache_ram_ttl = boost::none),
           decltype(cli::cache_ram_invalidation = boost::none),
           decltype(cli::cache_disk_size = boost::none),
           decltype(cli::fetch_endpoints = false),
           decltype(cli::fetch = false),
           decltype(cli::peer = Strings{}),
           decltype(cli::peers_file = boost::none),
           decltype(cli::push_endpoints = false),
           decltype(cli::push = false),
           decltype(cli::map_other_permissions = true),
           decltype(cli::publish = false),
           decltype(cli::advertise_host = Strings{}),
           decltype(cli::endpoints_file = boost::none),
           decltype(cli::port_file = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::listen = boost::none),
           decltype(cli::fetch_endpoints_interval = 300),
           decltype(cli::input = boost::none),
           decltype(cli::user = boost::none),
           decltype(cli::block_size = boost::none)>
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
    };
  }
}
