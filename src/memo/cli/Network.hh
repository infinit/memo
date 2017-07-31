#pragma once

#include <elle/das/cli.hh>

#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

namespace memo
{
  namespace cli
  {
    class Network
      : public Object<Network>
    {
    public:
      using Self = Network;
      Network(Memo& memo);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::delete_,
                                    cli::export_,
                                    cli::fetch,
                                    cli::import,
#ifndef ELLE_WINDOWS
                                    cli::inspect,
#endif
                                    cli::link,
                                    cli::list,
                                    cli::unlink,
                                    cli::list_services,
                                    cli::list_silos,
                                    cli::pull,
                                    cli::push,
                                    cli::run,
                                    cli::stat,
                                    cli::update));

      using Strings = std::vector<std::string>;

      /*---------------.
      | Mode: create.  |
      `---------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::silo = Strings{}),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::replication_factor = 1),
                 decltype(cli::eviction_delay = boost::optional<std::string>()),
                 decltype(cli::output = boost::optional<std::string>()),
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
                 decltype(cli::nodes = boost::optional<int>()),
                 decltype(cli::k = boost::optional<int>()),
                 decltype(cli::kelips_contact_timeout =
                          boost::optional<std::string>()),
                 decltype(cli::encrypt = boost::optional<std::string>()),
                 decltype(cli::protocol = boost::optional<std::string>()),
                 decltype(cli::tcp_heartbeat =
                          boost::optional<std::chrono::milliseconds>()),
                 decltype(cli::disable_encrypt_at_rest = false),
                 decltype(cli::disable_encrypt_rpc = false),
                 decltype(cli::disable_signature = false)),
           decltype(modes::mode_create)>
      create;
      void
      mode_create(
        std::string const& network_name,
        boost::optional<std::string> const& description = {},
        Strings const& silo = {},
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
        boost::optional<std::string> kelips_contact_timeout = boost::none,
        boost::optional<std::string> encrypt = boost::none,
        // Generic options
        boost::optional<std::string> protocol = boost::none,
        boost::optional<std::chrono::milliseconds> tcp_heartbeat = boost::none,
        bool disable_encrypt_at_rest = false,
        bool disable_encrypt_rpc = false,
        bool disable_signature = false);

      /*---------------.
      | Mode: delete.  |
      `---------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::pull = false),
                 decltype(cli::purge = false),
                 decltype(cli::unlink = false)),
           decltype(modes::mode_delete)>
      delete_;
      void
      mode_delete(std::string const& network_name,
                  bool pull = false,
                  bool purge = false,
                  bool unlink = false);

      /*---------------.
      | Mode: export.  |
      `---------------*/

      Mode<Self,
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

      Mode<Self,
           void (decltype(cli::name = boost::optional<std::string>())),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> const& network_name = {});

      /*---------------.
      | Mode: import.  |
      `---------------*/

      Mode<Self,
           void (decltype(cli::input = boost::optional<std::string>())),
           decltype(modes::mode_import)>
      import;
      void
      mode_import(boost::optional<std::string> const& input_name = {});


      /*----------------.
      | Mode: inspect.  |
      `----------------*/

#ifndef ELLE_WINDOWS
      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::output = boost::optional<std::string>()),
                 decltype(cli::status = false),
                 decltype(cli::peers = false),
                 decltype(cli::all = false),
                 decltype(cli::redundancy = false)),
           decltype(modes::mode_inspect)>
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

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::silo = Strings{}),
                 decltype(cli::output = boost::optional<std::string>()),
                 decltype(cli::node_id = boost::optional<std::string>())),
           decltype(modes::mode_link)>
      link;
      void
      mode_link(std::string const& network_name,
                Strings const& silos_names = {},
                boost::optional<std::string> const& output_name = {},
                boost::optional<std::string> const& node_id = {});


      /*-------------.
      | Mode: list.  |
      `-------------*/

      Mode<Self,
           void (),
           decltype(modes::mode_list)>
      list;
      void
      mode_list();


      /*----------------------.
      | Mode: list_services.  |
      `----------------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::peer = Strings()),
                 decltype(cli::async = false),
                 decltype(cli::cache = false),
                 decltype(cli::cache_ram_size = boost::optional<int>()),
                 decltype(cli::cache_ram_ttl = boost::optional<int>()),
                 decltype(cli::cache_ram_invalidation = boost::optional<int>()),
                 decltype(cli::cache_disk_size = boost::optional<uint64_t>()),
                 decltype(cli::fetch_endpoints = false),
                 decltype(cli::fetch = false),
                 decltype(cli::push_endpoints = false),
                 decltype(cli::push = false),
                 decltype(cli::publish = false),
                 decltype(cli::endpoints_file = boost::optional<std::string>()),
                 decltype(cli::port_file = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::peers_file = boost::optional<std::string>()),
                 decltype(cli::listen = boost::optional<std::string>()),
                 decltype(cli::fetch_endpoints_interval =
                          boost::optional<int>()),
                 decltype(cli::no_local_endpoints = false),
                 decltype(cli::no_public_endpoints = false),
                 decltype(cli::advertise_host = Strings())),
           decltype(modes::mode_list_services)>
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


      /*------------------.
      | Mode: list_silos. |
      `------------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_list_silos)>
      list_silos;
      void
      mode_list_silos(std::string const& network_name);


      /*-------------.
      | Mode: pull.  |
      `-------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::purge = false)),
           decltype(modes::mode_pull)>
      pull;
      void
      mode_pull(std::string const& network_name,
                bool purge = false);


      /*-------------.
      | Mode: push.  |
      `-------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_push)>
      push;
      void
      mode_push(std::string const& network_name);


      /*------------.
      | Mode: run.  |
      `------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::input = boost::optional<std::string>()),
#ifndef ELLE_WINDOWS
                 decltype(cli::daemon = false),
                 decltype(cli::monitoring = true),
#endif
                 decltype(cli::peer = Strings{}),
                 decltype(cli::async = false),
                 decltype(cli::cache = false),
                 decltype(cli::cache_ram_size = boost::optional<int>()),
                 decltype(cli::cache_ram_ttl = boost::optional<int>()),
                 decltype(cli::cache_ram_invalidation = boost::optional<int>()),
                 decltype(cli::cache_disk_size = boost::optional<uint64_t>()),
                 decltype(cli::fetch_endpoints = false),
                 decltype(cli::fetch = false),
                 decltype(cli::push_endpoints = false),
                 decltype(cli::push = false),
                 decltype(cli::publish = false),
                 decltype(cli::endpoints_file = boost::optional<std::string>()),
                 decltype(cli::port_file = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::peers_file = boost::optional<std::string>()),
                 decltype(cli::listen = boost::optional<std::string>()),
                 decltype(cli::fetch_endpoints_interval =
                          boost::optional<int>()),
                 decltype(cli::no_local_endpoints = false),
                 decltype(cli::no_public_endpoints = false),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::grpc = boost::optional<std::string>()),
                 decltype(cli::grpc_port_file = boost::optional<std::string>()),
                 // Options that used to be hidden.
                 decltype(cli::paxos_rebalancing_auto_expand =
                          boost::optional<bool>()),
                 decltype(cli::paxos_rebalancing_inspect =
                          boost::optional<bool>()),
                 decltype(cli::resign_on_shutdown =
                          boost::optional<bool>())),
           decltype(modes::mode_run)>
      run;
      void
      mode_run(std::string const& network_name,
               boost::optional<std::string> const& commands,
#ifndef ELLE_WINDOWS
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
               boost::optional<std::string> grpc = {},
               boost::optional<std::string> const& grpc_port_file = {},
               // Options that used to be hidden.
               boost::optional<bool> paxos_rebalancing_auto_expand = {},
               boost::optional<bool> paxos_rebalancing_inspect = {},
               boost::optional<bool> resign_on_shutdown = {});


      /*-------------.
      | Mode: stat.  |
      `-------------*/

      MODE(stat,
           decltype(cli::name)::Formal<std::string const&>);
      void
      mode_stat(std::string const& network_name);


      /*---------------.
      | Mode: unlink.  |
      `---------------*/

      MODE(unlink,
          decltype(cli::name)::Formal<std::string const&>);
      void
      mode_unlink(std::string const& network_name);


      /*---------------.
      | Mode: update.  |
      `---------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::port = boost::optional<int>()),
                 decltype(cli::output = boost::optional<std::string>()),
                 decltype(cli::push_network = false),
                 decltype(cli::push = false),
                 decltype(cli::admin_r = Strings{}),
                 decltype(cli::admin_rw = Strings{}),
                 decltype(cli::admin_remove = Strings{}),
                 decltype(cli::mountpoint = boost::optional<std::string>()),
                 decltype(cli::peer = Strings{}),
                 decltype(cli::protocol = boost::optional<std::string>())),
           decltype(modes::mode_update)>
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
                  Strings const& peer = Strings{},
                  boost::optional<std::string> const& protocol = boost::none);
    };
  }
}
