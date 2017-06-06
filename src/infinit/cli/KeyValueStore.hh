#pragma once

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class KeyValueStore
      : public Object<KeyValueStore>
    {
    public:
      using Self = KeyValueStore;
      KeyValueStore(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::delete_,
                                    cli::export_,
                                    cli::fetch,
                                    cli::import,
                                    cli::list,
                                    cli::pull,
                                    cli::push,
                                    cli::run));

      using Strings = std::vector<std::string>;

      /*---------------.
      | Mode: create.  |
      `---------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::push_key_value_store = false),
                 decltype(cli::output = boost::optional<std::string>()),
                 decltype(cli::push = false)),
           decltype(modes::mode_create)>
      create;
      void
      mode_create(std::string const& name,
                  std::string const& network,
                  boost::optional<std::string> description = {},
                  bool push_key_value_store = false,
                  boost::optional<std::string> output = {},
                  bool push = false);

      /*---------------.
      | Mode: delete.  |
      `---------------*/

      Mode<Self,
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
           void (decltype(cli::name = boost::optional<std::string>()),
                 decltype(cli::network = boost::optional<std::string>())),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> volume_name = {},
                 boost::optional<std::string> network_name = {});

      /*---------------.
      | Mode: import.  |
      `---------------*/

      Mode<Self,
           void (decltype(cli::input = boost::optional<std::string>())),
           decltype(modes::mode_import)>
      import;
      void
      mode_import(boost::optional<std::string> input_name = {});

      /*-------------.
      | Mode: list.  |
      `-------------*/

      Mode<Self,
           void (),
           decltype(modes::mode_list)>
      list;
      void
      mode_list();

      /*-------------.
      | Mode: pull.  |
      `-------------*/

      Mode<Self,
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

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>),
           decltype(modes::mode_push)>
      push;
      void
      mode_push(std::string const& name);

      /*------------.
      | Mode: run.  |
      `------------*/

      Mode<Self,
           void (decltype(cli::name)::Formal<std::string const&>,
                 decltype(cli::allow_root_creation = false),
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
                 decltype(cli::peers_file = boost::optional<std::string>()),
                 decltype(cli::listen = boost::optional<std::string>()),
                 decltype(cli::fetch_endpoints_interval =
                          boost::optional<int>()),
                 decltype(cli::no_local_endpoints = false),
                 decltype(cli::no_public_endpoints = false),
                 decltype(cli::advertise_host = Strings{}),
                 decltype(cli::grpc = boost::optional<std::string>()),
                 decltype(cli::grpc_port_file = boost::optional<std::string>())),
           decltype(modes::mode_run)>
      run;
      void
      mode_run(std::string const& name,
               bool allow_root_creation = false,
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
               boost::optional<std::string> const& peers_file = {},
               boost::optional<std::string> listen = {},
               boost::optional<int> fetch_endpoints_interval = {},
               bool no_local_endpoints = false,
               bool no_public_endpoints = false,
               Strings advertise_host = {},
               boost::optional<std::string> grpc = {},
               boost::optional<std::string> grpc_port_file = {});
    };
  }
}
