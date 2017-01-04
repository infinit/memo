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
    class Network
      : public Entity<Network>
    {
    public:
      Network(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::delete_,
                                    cli::export_,
                                    cli::fetch,
                                    cli::import,
                                    cli::link,
                                    cli::list,
                                    cli::unlink,
                                    cli::list_services,
                                    cli::list_storage,
                                    cli::pull,
                                    cli::push,
                                    cli::run,
                                    cli::update));

      using Strings = std::vector<std::string>;

      /*---------------.
      | Mode: create.  |
      `---------------*/
      using ModeCreate =
        Mode<decltype(binding(modes::mode_create,
                              cli::name,
                              cli::description = boost::none,
                              cli::storage = Strings{},
                              cli::port = boost::none,
                              cli::replication_factor = 1,
                              cli::eviction_delay = boost::none,
                              cli::output = boost::none,
                              cli::push_network = false,
                              cli::push = false,
                              cli::admin_r = Strings{},
                              cli::admin_rw = Strings{},
                              cli::peer = Strings{},
                              // Consensus types.
                              cli::paxos = false,
                              cli::no_consensus = false,
                              // Overlay types.
                              cli::kelips = false,
                              cli::kalimero = false,
                              cli::kouncil = false,
                              // Kelips options,
                              cli::nodes = boost::none,
                              cli::k = boost::none,
                              cli::kelips_contact_timeout = boost::none,
                              cli::encrypt = boost::none,
                              cli::protocol = boost::none))>;
      ModeCreate create;
      void
      mode_create(std::string const& network_name,
                  boost::optional<std::string> const& description = {},
                  Strings const& storage = {},
                  boost::optional<int> port = boost::none,
                  int replication_factor = 1,
                  boost::optional<std::string> const& eviction_delay = boost::none,
                  boost::optional<std::string> const& output_name = boost::none,
                  bool push_network = false,
                  bool push = false,
                  Strings const& admin_r = {},
                  Strings const& admin_rw = {},
                  Strings const& peer = {},
                  // Consensus types.
                  bool paxos = false,
                  bool no_consensus = false,
                  // Overlay types.
                  bool kelips = false,
                  bool kalimero = false,
                  bool kouncil = false,
                  // Kelips options,
                  boost::optional<int> nodes = boost::none,
                  boost::optional<int> k = boost::none,
                  boost::optional<std::string> const& kelips_contact_timeout = boost::none,
                  boost::optional<std::string> const& encrypt = boost::none,
                  boost::optional<std::string> const& protocol = boost::none);


      /*---------------.
      | Mode: delete.  |
      `---------------*/

      using ModeDelete =
        Mode<decltype(binding(modes::mode_delete,
                              cli::name,
                              cli::pull = false,
                              cli::purge = false,
                              cli::unlink = false))>;
      ModeDelete delete_;
      void
      mode_delete(std::string const& network_name,
                  bool pull = false,
                  bool purge = false,
                  bool unlink = false);

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
                              cli::name = boost::none))>;
      ModeFetch fetch;
      void
      mode_fetch(boost::optional<std::string> const& network_name = {});


      /*---------------.
      | Mode: import.  |
      `---------------*/
      using ModeImport =
        Mode<decltype(binding(modes::mode_import,
                              cli::input = boost::none))>;
      ModeImport import;
      void
      mode_import(boost::optional<std::string> const& input_name = {});


      /*-------------.
      | Mode: link.  |
      `-------------*/
      using ModeLink =
        Mode<decltype(binding(modes::mode_link,
                              cli::name,
                              cli::storage = Strings{},
                              cli::output = boost::none))>;
      ModeLink link;
      void
      mode_link(std::string const& network_name,
                Strings const& storage_names = {},
                boost::optional<std::string> const& output_name = {});


      /*-------------.
      | Mode: list.  |
      `-------------*/
      using ModeList =
        Mode<decltype(binding(modes::mode_list))>;
      ModeList list;
      void
      mode_list();


      /*----------------------.
      | Mode: list_services.  |
      `----------------------*/
      using ModeListServices =
        Mode<decltype(binding(modes::mode_list_services,
                              cli::name,
                              cli::peer = boost::none,
                              cli::async = false,
                              cli::cache = false,
                              cli::cache_ram_size = boost::none,
                              cli::cache_ram_ttl = boost::none,
                              cli::cache_ram_invalidation = boost::none,
                              cli::cache_disk_size = boost::none,
                              cli::fetch_endpoints = false,
                              cli::fetch = false,
                              cli::push_endpoints = false,
                              cli::push = false,
                              cli::publish = false,
                              cli::endpoints_file = boost::none,
                              cli::port_file = boost::none,
                              cli::port = boost::none,
                              cli::peers_file = boost::none,
                              cli::listen = boost::none,
                              cli::fetch_endpoints_interval = boost::none,
                              cli::no_local_endpoints = false,
                              cli::no_public_endpoints = false,
                              cli::advertise_host = boost::none))>;
      ModeListServices list_services;
      void
      mode_list_services(std::string const& network_name,
                         Strings peer = {},
                         bool async = false,
                         bool cache = false,
                         boost::optional<int> cache_ram_size = {},
                         boost::optional<int> cache_ram_ttl = {},
                         boost::optional<int> cache_ram_invalidation = {},
                         boost::optional<uint64_t> cache_disk_size = {},
                         bool fetch_endpoints = false,
                         bool fetch = false,
                         bool push_endpoints = false,
                         bool push = false,
                         bool publish = false,
                         boost::optional<std::string> const& endpoints_file = {},
                         boost::optional<std::string> const& port_file = {},
                         boost::optional<int> port = {},
                         boost::optional<std::string> const& peers_file = {},
                         boost::optional<std::string> listen = {},
                         boost::optional<int> fetch_endpoints_interval = {},
                         bool no_local_endpoints = false,
                         bool no_public_endpoints = false,
                         Strings advertise_host = {});


      /*---------------------.
      | Mode: list_storage.  |
      `---------------------*/
      using ModeListStorage =
        Mode<decltype(binding(modes::mode_list_storage,
                              cli::name))>;
      ModeListStorage list_storage;
      void
      mode_list_storage(std::string const& network_name);


      /*-------------.
      | Mode: pull.  |
      `-------------*/
      using ModePull =
        Mode<decltype(binding(modes::mode_pull,
                              cli::name,
                              cli::purge = false))>;
      ModePull pull;
      void
      mode_pull(std::string const& network_name,
                bool purge = false);


      /*-------------.
      | Mode: push.  |
      `-------------*/
      using ModePush =
        Mode<decltype(binding(modes::mode_push,
                              cli::name))>;
      ModePush push;
      void
      mode_push(std::string const& network_name);


      /*------------.
      | Mode: run.  |
      `------------*/
      using ModeRun =
        Mode<decltype(binding(modes::mode_run,
                              cli::name,
                              cli::input = boost::none,
#ifndef INFINIT_WINDOWS
                              cli::daemon = false,
                              cli::monitoring = true,
#endif
                              cli::peer = Strings{},
                              cli::async = false,
                              cli::cache = false,
                              cli::cache_ram_size = boost::none,
                              cli::cache_ram_ttl = boost::none,
                              cli::cache_ram_invalidation = boost::none,
                              cli::cache_disk_size = boost::none,
                              cli::fetch_endpoints = false,
                              cli::fetch = false,
                              cli::push_endpoints = false,
                              cli::push = false,
                              cli::publish = false,
                              cli::endpoints_file = boost::none,
                              cli::port_file = boost::none,
                              cli::port = boost::none,
                              cli::peers_file = boost::none,
                              cli::listen = boost::none,
                              cli::fetch_endpoints_interval = boost::none,
                              cli::no_local_endpoints = false,
                              cli::no_public_endpoints = false,
                              cli::advertise_host = Strings{},
                              // Hisymdden options.
                              cli::paxos_rebalancing_auto_expand = boost::none,
                              cli::paxos_rebalancing_inspect = boost::none))>;
      ModeRun run;

      void
      mode_run(std::string const& network_name,
               boost::optional<std::string> const& commands,
#ifndef INFINIT_WINDOWS
               bool daemon = false,
               bool monitoring = true,
#endif
               Strings peer = {},
               bool async = false,
               bool cache = false,
               boost::optional<int> cache_ram_size = {},
               boost::optional<int> cache_ram_ttl = {},
               boost::optional<int> cache_ram_invalidation = {},
               boost::optional<uint64_t> cache_disk_size = {},
               bool fetch_endpoints = false,
               bool fetch = false,
               bool push_endpoints = false,
               bool push = false,
               bool publish = false,
               boost::optional<std::string> const& endpoint_file = {},
               boost::optional<std::string> const& port_file = {},
               boost::optional<int> port = {},
               boost::optional<std::string> const& peers_file = {},
               boost::optional<std::string> listen = {},
               boost::optional<int> fetch_endpoints_interval = {},
               bool no_local_endpoints = false,
               bool no_public_endpoints = false,
               Strings advertise_host = {},
               // Hidden options.
               boost::optional<bool> paxos_rebalancing_auto_expand = {},
               boost::optional<bool> paxos_rebalancing_inspect = {});


      /*--------------.
      | Mode: stats.  |
      `--------------*/
      using ModeStats =
        Mode<decltype(binding(modes::mode_stats,
                              cli::name))>;
      ModeStats stats;
      void
      mode_stats(std::string const& network_name);


      /*---------------.
      | Mode: unlink.  |
      `---------------*/
      using ModeUnlink =
        Mode<decltype(binding(modes::mode_unlink,
                              cli::name))>;
      ModeUnlink unlink;
      void
      mode_unlink(std::string const& network_name);


      /*---------------.
      | Mode: update.  |
      `---------------*/

      using ModeUpdate =
        Mode<decltype(binding(modes::mode_update,
                              cli::name,
                              cli::description = boost::none,
                              cli::port = boost::none,
                              cli::output = boost::none,
                              cli::push_network = false,
                              cli::push = false,
                              cli::admin_r = Strings{},
                              cli::admin_rw = Strings{},
                              cli::admin_remove = Strings{},
                              cli::mountpoint = boost::none,
                              cli::peer = Strings{}))>;
      ModeUpdate update;
      void
      mode_update(std::string const& network_name,
                  boost::optional<std::string> const& description = {},
                  boost::optional<int> port = {},
                  boost::optional<std::string> const& output_name = {},
                  bool push_network = false,
                  bool push = false,
                  Strings const& admin_r = Strings{},
                  Strings const& admin_rw = Strings{},
                  Strings const& admin_remove = Strings{},
                  boost::optional<std::string> const& mountpoint = {},
                  Strings const& peer = Strings{});
    };
  }
}
