#include <memo/cli/KeyValueStore.hh>

#include <elle/reactor/network/resolve.hh>

#include <memo/cli/Memo.hh>
#include <memo/cli/utility.hh>
#include <memo/grpc/grpc.hh>
#include <memo/kvs/lib/libkvs.h>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/Local.hh>
#include <memo/silo/Silo.hh>

ELLE_LOG_COMPONENT("cli.key-value-store");

namespace memo
{
  namespace cli
  {
    KeyValueStore::KeyValueStore(Memo& cli)
      : Object(cli)
      , create(*this,
               "Create a key-value store",
               cli::name,
               cli::network,
               cli::description = boost::none,
               cli::push_key_value_store = false,
               cli::output = boost::none,
               cli::push = false)
      , delete_(*this,
                "Delete a key-value store locally",
                cli::name,
                cli::pull = false,
                cli::purge = false)
      , export_(*this,
                "Export a key-value store for someone else to import",
                cli::name,
                cli::output = boost::none)
      , fetch(*this,
              "Fetch a key-value store from {hub}",
              cli::name = boost::none,
              cli::network = boost::none)
      , import(*this,
               "Import a key-value store",
               cli::input = boost::none)
      , list(*this, "List key-value stores")
      , pull(*this,
             "Remove a key-value store from {hub}",
             cli::name,
             cli::purge = false)
      , push(*this,
             "Push a key-value store to {hub}",
             cli::name)
      , run(*this,
            "Run a key-value store",
            cli::name,
            cli::grpc,
            cli::allow_root_creation = false,
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
            cli::peers_file = boost::none,
            cli::port = boost::none,
            cli::listen = boost::none,
            cli::fetch_endpoints_interval = boost::none,
            cli::no_local_endpoints = false,
            cli::no_public_endpoints = false,
            cli::advertise_host = Strings{},
            cli::grpc_port_file = boost::none)
    {}

    /*---------------.
    | Mode: create.  |
    `---------------*/

    void
    KeyValueStore::mode_create(std::string const& unqualified_name,
                               std::string const& network_name,
                               boost::optional<std::string> description,
                               bool push_key_value_store,
                               boost::optional<std::string> output_name,
                               bool push)
    {
      ELLE_TRACE_SCOPE("create");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto name = memo.qualified_name(unqualified_name, owner);
      auto network = memo.network_get(network_name, owner);

      auto kvs = memo::KeyValueStore(name, network.name, description);
      if (output_name)
      {
        auto output = cli.get_output(output_name);
        auto s = elle::serialization::json::SerializerOut(*output, false);
        s.serialize_forward(kvs);
      }
      else
      {
        memo.key_value_store_save(kvs);
      }
      if (push || push_key_value_store)
        memo.hub_push("kvs", name, kvs, owner);
    }

    /*---------------.
    | Mode: delete.  |
    `---------------*/

    void
    KeyValueStore::mode_delete(std::string const& unqualified_name,
                               bool pull,
                               bool purge)
    {
      ELLE_TRACE_SCOPE("delete");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto name = memo.qualified_name(unqualified_name, owner);
      auto kvs = memo.key_value_store_get(name);
      if (purge)
      { /* Do nothing */ }
      if (pull)
        memo.hub_delete("kvs", name, owner, true, purge);
      memo.key_value_store_delete(kvs);
    }

    /*---------------.
    | Mode: export.  |
    `---------------*/

    void
    KeyValueStore::mode_export(std::string const& unqualified_name,
                               boost::optional<std::string> const& output_name)
    {
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto name = memo.qualified_name(unqualified_name, owner);
      auto kvs = memo.key_value_store_get(name);
      auto output = cli.get_output(output_name);
      {
        auto s = elle::serialization::json::SerializerOut(*output, false);
        s.serialize_forward(kvs);
      }
      cli.report_exported(*output, "kvs", kvs.name);
    }

    /*--------------.
    | Mode: fetch.  |
    `--------------*/

    void
    KeyValueStore::mode_fetch(boost::optional<std::string> unqualified_name,
                              boost::optional<std::string> network_name)
    {
      ELLE_TRACE_SCOPE("fetch");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      using KeyValueStoresMap
        = std::unordered_map<std::string, std::vector<memo::KeyValueStore>>;
      if (unqualified_name)
      {
        auto name = memo.qualified_name(*unqualified_name, owner);
        auto desc =
          memo.hub_fetch<memo::KeyValueStore>("kvs", name);
        memo.key_value_store_save(std::move(desc));
      }
      else if (network_name)
      {
        // Fetch all key-value stores for network.
        auto net_name = memo.qualified_name(*network_name, owner);
        auto res = memo.hub_fetch<KeyValueStoresMap>(
            elle::sprintf("networks/%s/kvs", net_name),
            "kvs for network",
            net_name);
        for (auto const& k: res["kvs"])
          memo.key_value_store_save(k);
      }
      else
      {
        // Fetch all key-value stores for owner.
        auto res = memo.hub_fetch<KeyValueStoresMap>(
            elle::sprintf("users/%s/kvs", owner.name),
            "kvs for user",
            owner.name,
            owner);
        for (auto const& k: res["kvs"])
          memo.key_value_store_save(k, true);
      }
    }

    /*---------------.
    | Mode: import.  |
    `---------------*/

    void
    KeyValueStore::mode_import(boost::optional<std::string> input_name)
    {
      ELLE_TRACE_SCOPE("import");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto input = this->cli().get_input(input_name);
      auto s = elle::serialization::json::SerializerIn(*input, false);
      auto kvs = memo::KeyValueStore(s);
      memo.key_value_store_save(kvs);
      cli.report_imported("kvs", kvs.name);
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/

    void
    KeyValueStore::mode_list()
    {
      ELLE_TRACE_SCOPE("list");
      auto& cli = this->cli();
      auto& memo = cli.backend();

      if (cli.script())
      {
        auto const l = elle::json::make_array(memo.key_value_stores_get(),
                                              [&](auto const& kvs) {
          auto res = elle::json::Object
            {
              {"name", static_cast<std::string>(kvs.name)},
              {"network", kvs.network},
            };
          if (kvs.description)
            res["description"] = *kvs.description;
          return res;
          });
        elle::json::write(std::cout, l);
      }
      else
        for (auto const& k: memo.key_value_stores_get())
        {
          std::cout << k.name;
          if (k.description)
            std::cout << " \"" << k.description.get() << "\"";
          std::cout << ": network " << k.network;
          std::cout << std::endl;
        }
    }

    /*-------------.
    | Mode: pull.  |
    `-------------*/

    void
    KeyValueStore::mode_pull(std::string const& unqualified_name,
                             bool purge)
    {
      ELLE_TRACE_SCOPE("pull");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto const name = memo.qualified_name(unqualified_name, owner);
      memo.hub_delete("kvs", name, owner, false, purge);
    }


    /*-------------.
    | Mode: push.  |
    `-------------*/

    void
    KeyValueStore::mode_push(std::string const& unqualified_name)
    {
      ELLE_TRACE_SCOPE("push");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto const name = memo.qualified_name(unqualified_name, owner);
      auto kvs = memo.key_value_store_get(name);
      auto network = memo.network_get(kvs.network, owner);
      memo.hub_push("kvs", name, kvs, owner);
    }

    /*------------.
    | Mode: run.  |
    `------------*/

    void
    KeyValueStore::mode_run(std::string const& unqualified_name,
                            std::string const& grpc,
                            bool allow_root_creation,
                            Strings peer,
                            bool async,
                            bool cache,
                            boost::optional<int> cache_ram_size,
                            boost::optional<int> cache_ram_ttl,
                            boost::optional<int> cache_ram_invalidation,
                            boost::optional<uint64_t> cache_disk_size,
                            bool fetch_endpoints,
                            bool fetch,
                            bool push_endpoints,
                            bool push,
                            bool publish,
                            boost::optional<std::string> const& endpoints_file,
                            boost::optional<std::string> const& peers_file,
                            boost::optional<int> port,
                            boost::optional<std::string> listen,
                            boost::optional<int> fetch_endpoints_interval,
                            bool no_local_endpoints,
                            bool no_public_endpoints,
                            Strings advertise_host,
                            boost::optional<std::string> grpc_port_file)
    {
      ELLE_TRACE_SCOPE("run");
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto const name = memo.qualified_name(unqualified_name, owner);
      auto kvs = memo.key_value_store_get(name);
      auto network = memo.network_get(kvs.network, owner);

      network.ensure_allowed(owner, "run", "kvs");
      cache |= (cache_ram_size || cache_ram_ttl
                || cache_ram_invalidation || cache_disk_size);
      auto const listen_address
        = listen
        ? boost::asio::ip::address::from_string(*listen)
        : boost::optional<boost::asio::ip::address>{};
      auto dht = network.run(
        owner,
        false,
        cache, cache_ram_size, cache_ram_ttl, cache_ram_invalidation,
        async, cache_disk_size, cli.compatibility_version(),
        port,
        listen_address);
      hook_stats_signals(*dht);
      int dht_grpc_port = 0;
      auto dht_grpc_thread = std::make_unique<elle::reactor::Thread>
        ("DHT gRPC",
        [dht = dht.get(), &dht_grpc_port] {
          memo::grpc::serve_grpc(
            *dht, "127.0.0.1:0", &dht_grpc_port);
      });
      // Wait for DHT gRPC server to be running.
      while (dht_grpc_port == 0)
        elle::reactor::sleep(100ms);
      if (peers_file)
      {
        auto more_peers = hook_peer_discovery(*dht, *peers_file);
        ELLE_TRACE("Peer list file got %s peers", more_peers.size());
        if (!more_peers.empty())
          dht->overlay()->discover(more_peers);
      }
      if (!peer.empty())
        dht->overlay()->discover(parse_peers(peer));
      // Only push if we have are contributing storage.
      bool push_p = (push || publish)
        && dht->local() && dht->local()->storage();
      if (!dht->local() && push_p)
        elle::err("network %s is client only since no storage is attached",
                  name);
      if (dht->local() && endpoints_file)
        endpoints_to_file(dht->local()->server_endpoints(), *endpoints_file);
      auto run = [&]
        {
          elle::reactor::Thread::unique_ptr poll_thread;
          if (fetch || publish)
          {
            memo::model::NodeLocations eps;
            network.hub_fetch_endpoints(eps);
            dht->overlay()->discover(eps);
            if (fetch_endpoints_interval && *fetch_endpoints_interval > 0)
              poll_thread =
                network.make_poll_hub_thread(*dht, eps,
                                             *fetch_endpoints_interval);
          }
        };
      if (push_p)
        elle::With<InterfacePublisher>(
          memo,
          network, owner, dht->id(),
          dht->local()->server_endpoint().port(),
          advertise_host,
          no_local_endpoints,
          no_public_endpoints) << [&]
          {
            run();
          };
      else
        run();
      cli.report_action("running", "network", network.name);
      auto dht_ep = elle::sprintf("127.0.0.1:%s", dht_grpc_port);
      auto go_str = [] (std::string const& str) {
        return GoString{str.c_str(), static_cast<GoInt>(str.size())};
      };
      GoInt kv_grpc_port = 0;
      auto port_writer_thread = std::unique_ptr<elle::reactor::Thread>();
      elle::With<elle::Finally>([&]
      {
        if (grpc_port_file)
        {
          boost::system::error_code ec;
          bfs::remove(bfs::path(*grpc_port_file), ec);
        }
      }) << [&] (elle::Finally& f)
      {
        if (grpc_port_file)
        {
          port_writer_thread.reset(new elle::reactor::Thread(
            elle::reactor::scheduler(),
            "port file writer",
            [&] {
              while (kv_grpc_port == 0)
                elle::reactor::sleep(100ms);
              port_to_file(kv_grpc_port, *grpc_port_file);
            }));
        }
        static const auto signals = {SIGINT, SIGTERM
#ifndef ELLE_WINDOWS
                                     , SIGQUIT
#endif
        };
        for (auto s: signals)
          elle::reactor::scheduler().signal_handle(
            s,
            []
            {
              ELLE_DEBUG("stopping kvs");
              StopServer();
            });
        elle::reactor::background([&] {
          cli.report_action("running", "kvs", name);
          RunServer(
            go_str(name), go_str(dht_ep), go_str(grpc), allow_root_creation,
            &kv_grpc_port);
        });
      };
    }
  }
}
