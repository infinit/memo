#pragma once

#include <das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Network
      : public Object<Network>
    {
    public:
      Network(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::delete_,
                                    cli::export_,
                                    cli::fetch,
                                    cli::import,
#ifndef INFINIT_WINDOWS
                                    cli::inspect,
#endif
                                    cli::link,
                                    cli::list,
                                    cli::unlink,
                                    cli::list_services,
                                    cli::list_storage,
                                    cli::pull,
                                    cli::push,
                                    cli::run,
                                    cli::stats,
                                    cli::update));

      using Strings = std::vector<std::string>;

      /*---------------.
      | Mode: create.  |
      `---------------*/

      Mode<Network,
           decltype(modes::mode_create),
           decltype(cli::name),
           decltype(cli::description = boost::none),
           decltype(cli::storage = Strings{}),
           decltype(cli::port = boost::none),
           decltype(cli::replication_factor = 1),
           decltype(cli::eviction_delay = boost::none),
           decltype(cli::output = boost::none),
           decltype(cli::push_network = false),
           decltype(cli::push = false),
           decltype(cli::admin_r = Strings{}),
           decltype(cli::admin_rw = Strings{}),
           decltype(cli::peer = Strings{}),
           // Consensus types.
           decltype(cli::paxos = false),
           decltype(cli::no_consensus = false),
           // Overlay types.
           decltype(cli::kelips = false),
           decltype(cli::kalimero = false),
           decltype(cli::kouncil = false),
           // Kelips options.
           decltype(cli::nodes = boost::none),
           decltype(cli::k = boost::none),
           decltype(cli::kelips_contact_timeout = boost::none),
           decltype(cli::encrypt = boost::none),
           decltype(cli::protocol = boost::none)>
      create;
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

      Mode<Network,
           decltype(modes::mode_delete),
           decltype(cli::name),
           decltype(cli::pull = false),
           decltype(cli::purge = false),
           decltype(cli::unlink = false)>
      delete_;
      void
      mode_delete(std::string const& network_name,
                  bool pull = false,
                  bool purge = false,
                  bool unlink = false);

      /*---------------.
      | Mode: export.  |
      `---------------*/

      Mode<Network,
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

      Mode<Network,
           decltype(modes::mode_fetch),
           decltype(cli::name = boost::none)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> const& network_name = {});

      /*---------------.
      | Mode: import.  |
      `---------------*/

      Mode<Network,
           decltype(modes::mode_import),
           decltype(cli::input = boost::none)>
      import;
      void
      mode_import(boost::optional<std::string> const& input_name = {});


      /*----------------.
      | Mode: inspect.  |
      `----------------*/

#ifndef INFINIT_WINDOWS
      Mode<Network,
           decltype(modes::mode_inspect),
           decltype(cli::name),
           decltype(cli::output = boost::none),
           decltype(cli::status = false),
           decltype(cli::peers = false),
           decltype(cli::all = false),
           decltype(cli::redundancy = false)>
      inspect;
      void
      mode_inspect(std::string const& network_name,
                   boost::optional<std::string> const& output_name = {},
                   bool status = false,
                   bool peers = false,
                   bool all = false,
                   bool redundancy = false);
#endif


      /*-------------.
      | Mode: link.  |
      `-------------*/

      Mode<Network,
           decltype(modes::mode_link),
           decltype(cli::name),
           decltype(cli::storage = Strings{}),
           decltype(cli::output = boost::none)>
      link;
      void
      mode_link(std::string const& network_name,
                Strings const& storage_names = {},
                boost::optional<std::string> const& output_name = {});


      /*-------------.
      | Mode: list.  |
      `-------------*/

      Mode<Network,
           decltype(modes::mode_list)>
      list;
      void
      mode_list();


      /*----------------------.
      | Mode: list_services.  |
      `----------------------*/

      Mode<Network,
           decltype(modes::mode_list_services),
           decltype(cli::name),
           decltype(cli::peer = boost::none),
           decltype(cli::async = false),
           decltype(cli::cache = false),
           decltype(cli::cache_ram_size = boost::none),
           decltype(cli::cache_ram_ttl = boost::none),
           decltype(cli::cache_ram_invalidation = boost::none),
           decltype(cli::cache_disk_size = boost::none),
           decltype(cli::fetch_endpoints = false),
           decltype(cli::fetch = false),
           decltype(cli::push_endpoints = false),
           decltype(cli::push = false),
           decltype(cli::publish = false),
           decltype(cli::endpoints_file = boost::none),
           decltype(cli::port_file = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::peers_file = boost::none),
           decltype(cli::listen = boost::none),
           decltype(cli::fetch_endpoints_interval = boost::none),
           decltype(cli::no_local_endpoints = false),
           decltype(cli::no_public_endpoints = false),
           decltype(cli::advertise_host = boost::none)>
      list_services;
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

      Mode<Network,
           decltype(modes::mode_list_storage),
           decltype(cli::name)>
      list_storage;
      void
      mode_list_storage(std::string const& network_name);


      /*-------------.
      | Mode: pull.  |
      `-------------*/

      Mode<Network,
           decltype(modes::mode_pull),
           decltype(cli::name),
           decltype(cli::purge = false)>
      pull;
      void
      mode_pull(std::string const& network_name,
                bool purge = false);


      /*-------------.
      | Mode: push.  |
      `-------------*/

      Mode<Network,
           decltype(modes::mode_push),
           decltype(cli::name)>
      push;
      void
      mode_push(std::string const& network_name);


      /*------------.
      | Mode: run.  |
      `------------*/

      Mode<Network,
           decltype(modes::mode_run),
           decltype(cli::name),
           decltype(cli::input = boost::none),
#ifndef INFINIT_WINDOWS
           decltype(cli::daemon = false),
           decltype(cli::monitoring = true),
#endif
           decltype(cli::peer = Strings{}),
           decltype(cli::async = false),
           decltype(cli::cache = false),
           decltype(cli::cache_ram_size = boost::none),
           decltype(cli::cache_ram_ttl = boost::none),
           decltype(cli::cache_ram_invalidation = boost::none),
           decltype(cli::cache_disk_size = boost::none),
           decltype(cli::fetch_endpoints = false),
           decltype(cli::fetch = false),
           decltype(cli::push_endpoints = false),
           decltype(cli::push = false),
           decltype(cli::publish = false),
           decltype(cli::endpoints_file = boost::none),
           decltype(cli::port_file = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::peers_file = boost::none),
           decltype(cli::listen = boost::none),
           decltype(cli::fetch_endpoints_interval = boost::none),
           decltype(cli::no_local_endpoints = false),
           decltype(cli::no_public_endpoints = false),
           decltype(cli::advertise_host = Strings{}),
           // Hidden options.
           decltype(cli::paxos_rebalancing_auto_expand = boost::none),
           decltype(cli::paxos_rebalancing_inspect = boost::none)>
      run;
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

      Mode<Network,
           decltype(modes::mode_stats),
           decltype(cli::name)>
      stats;
      void
      mode_stats(std::string const& network_name);


      /*---------------.
      | Mode: unlink.  |
      `---------------*/

      Mode<Network,
           decltype(modes::mode_unlink),
           decltype(cli::name)>
      unlink;
      void
      mode_unlink(std::string const& network_name);


      /*---------------.
      | Mode: update.  |
      `---------------*/

      Mode<Network,
           decltype(modes::mode_update),
           decltype(cli::name),
           decltype(cli::description = boost::none),
           decltype(cli::port = boost::none),
           decltype(cli::output = boost::none),
           decltype(cli::push_network = false),
           decltype(cli::push = false),
           decltype(cli::admin_r = Strings{}),
           decltype(cli::admin_rw = Strings{}),
           decltype(cli::admin_remove = Strings{}),
           decltype(cli::mountpoint = boost::none),
           decltype(cli::peer = Strings{})>
      update;
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
