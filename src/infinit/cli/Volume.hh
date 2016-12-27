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
    class Volume
      : public Entity<Volume>
    {
    public:
      Volume(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::export_,
                                    cli::run));

      using Strings = std::vector<std::string>;

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
                  bool push_volume = false,
                  boost::optional<std::string> output = {},
                  boost::optional<std::string> default_permissions = {},
                  bool register_service = false,
                  bool allow_root_creation = false,
                  boost::optional<std::string> mountpoint = {},
                  bool readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                  boost::optional<std::string> mount_name = {},
#endif
#ifdef INFINIT_MACOSX
                  boost::optional<std::string> mount_icon = {},
                  bool finder_sidebar = false,
#endif
                  bool async = false,
#ifndef INFINIT_WINDOWS
                  bool daemon = false,
#endif
                  bool monitoring = true,
                  Strings fuse_option = {},
                  bool cache = false,
                  boost::optional<int> cache_ram_size = {},
                  boost::optional<int> cache_ram_ttl = {},
                  boost::optional<int> cache_ram_invalidation = {},
                  boost::optional<int> cache_disk_size = {},
                  bool fetch_endpoints = false,
                  bool fetch = false,
                  Strings peer = {},
                  boost::optional<std::string> peers_file = {},
                  bool push_endpoints = false,
                  bool push = false,
                  bool publish = false,
                  Strings advertise_host = {},
                  boost::optional<std::string> endpoints_file = {},
                  boost::optional<std::string> port_file = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> listen = {},
                  int fetch_endpoints_interval = 300,
                  boost::optional<std::string> input = {});


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



      /*------.
      | Run.  |
      `------*/
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
               bool readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
               boost::optional<std::string> mount_name = {},
#endif
#ifdef INFINIT_MACOSX
               boost::optional<std::string> mount_icon = {},
               bool finder_sidebar = false,
#endif
               bool async = false,
#ifndef INFINIT_WINDOWS
               bool daemon = false,
#endif
               bool monitoring = true,
               Strings fuse_option = {},
               bool cache = false,
               boost::optional<int> cache_ram_size = {},
               boost::optional<int> cache_ram_ttl = {},
               boost::optional<int> cache_ram_invalidation = {},
               boost::optional<int> cache_disk_size = {},
               bool fetch_endpoints = false,
               bool fetch = false,
               Strings peer = {},
               boost::optional<std::string> peers_file = {},
               bool push_endpoints = false,
               bool register_service = false,
               bool no_local_endpoints = false,
               bool no_public_endpoints = false,
               bool push = false,
               bool map_other_permissions = true,
               bool publish = false,
               Strings advertise_host = {},
               boost::optional<std::string> endpoints_file = {},
               boost::optional<std::string> port_file = {},
               boost::optional<int> port = {},
               boost::optional<std::string> listen = {},
               int fetch_endpoints_interval = 300,
               boost::optional<std::string> input = {},
               bool disable_UTF_8_conversion = false);
    };
  }
}
