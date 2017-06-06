#include <infinit/cli/KeyValueStore.hh>

#include <elle/reactor/network/resolve.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>
#include <infinit/grpc/grpc.hh>
#include <infinit/kv/lib/libkv.h>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/silo/Silo.hh>

ELLE_LOG_COMPONENT("cli.key-value-store");

namespace infinit
{
  namespace cli
  {
    KeyValueStore::KeyValueStore(Infinit& infinit)
      : Object(infinit)
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
            cli::listen = boost::none,
            cli::fetch_endpoints_interval = boost::none,
            cli::no_local_endpoints = false,
            cli::no_public_endpoints = false,
            cli::advertise_host = Strings{},
            cli::grpc = boost::none,
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(unqualified_name, owner);
      auto network = ifnt.network_get(network_name, owner);

      auto kvs = infinit::KeyValueStore(name, network.name, description);
      if (output_name)
      {
        auto output = cli.get_output(output_name);
        auto s = elle::serialization::json::SerializerOut(*output, false);
        s.serialize_forward(kvs);
      }
      else
      {
        ifnt.key_value_store_save(kvs);
      }
      if (push || push_key_value_store)
        ifnt.beyond_push("key-value-store", name, kvs, owner);
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(unqualified_name, owner);
      auto kvs = ifnt.key_value_store_get(name);
      if (purge)
      { /* Do nothing */ }
      if (pull)
        ifnt.beyond_delete("key-value-store", name, owner, true, purge);
      ifnt.key_value_store_delete(kvs);
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(unqualified_name, owner);
      auto kvs = ifnt.key_value_store_get(name);
      auto output = cli.get_output(output_name);
      {
        auto s = elle::serialization::json::SerializerOut(*output, false);
        s.serialize_forward(kvs);
      }
      cli.report_exported(*output, "key-value store", kvs.name);
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      using KeyValueStoresMap
        = std::unordered_map<std::string, std::vector<infinit::KeyValueStore>>;
      if (unqualified_name)
      {
        auto name = ifnt.qualified_name(*unqualified_name, owner);
        auto desc =
          ifnt.beyond_fetch<infinit::KeyValueStore>("key-value-store", name);
        ifnt.key_value_store_save(std::move(desc));
      }
      else if (network_name)
      {
        // Fetch all key-value stores for network.
        auto net_name = ifnt.qualified_name(*network_name, owner);
        auto res = ifnt.beyond_fetch<KeyValueStoresMap>(
            elle::sprintf("networks/%s/key-value-stores", net_name),
            "key-value stores for network",
            net_name);
        for (auto const& k: res["key-value-stores"])
          ifnt.key_value_store_save(k);
      }
      else
      {
        // Fetch all key-value stores for owner.
        auto res = ifnt.beyond_fetch<KeyValueStoresMap>(
            elle::sprintf("users/%s/key-value-stores", owner.name),
            "key-value stores for user",
            owner.name,
            owner);
        for (auto const& k: res["key-value-stores"])
          ifnt.key_value_store_save(k, true);
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
      auto& ifnt = cli.infinit();
      auto input = this->cli().get_input(input_name);
      auto s = elle::serialization::json::SerializerIn(*input, false);
      auto kvs = infinit::KeyValueStore(s);
      ifnt.key_value_store_save(kvs);
      cli.report_imported("key-value store", kvs.name);
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/

    void
    KeyValueStore::mode_list()
    {
      ELLE_TRACE_SCOPE("list");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();

      if (cli.script())
      {
        auto l = elle::json::make_array(ifnt.key_value_stores_get(),
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
        for (auto const& k: ifnt.key_value_stores_get())
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto const name = ifnt.qualified_name(unqualified_name, owner);
      ifnt.beyond_delete("key-value-store", name, owner, false, purge);
    }


    /*-------------.
    | Mode: push.  |
    `-------------*/

    void
    KeyValueStore::mode_push(std::string const& unqualified_name)
    {
      ELLE_TRACE_SCOPE("push");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto const name = ifnt.qualified_name(unqualified_name, owner);
      auto kvs = ifnt.key_value_store_get(name);
      auto network = ifnt.network_get(kvs.network, owner);
      ifnt.beyond_push("key-value-store", name, kvs, owner);

    }

    /*------------.
    | Mode: run.  |
    `------------*/

    /// Turn a list of addresses (e.g., `foo.bar.fr:http`) and/or
    /// filenames that contains such addresses, into a list of
    /// Endpoints.
    ///
    /// Yes, a list of Endpoints, not a list of Endpoint, because
    /// foo.bar.fr might actually denote several hosts, and we want
    /// to reach each one individually.
    std::vector<infinit::model::Endpoints>
    parse_peers(std::vector<std::string> const& peers)
    {
      auto res = std::vector<infinit::model::Endpoints>{};
      for (auto const& peer: peers)
      {
        auto const eps
          = bfs::exists(peer)
          ? model::endpoints_from_file(peer)
          : elle::reactor::network::resolve_udp_repr(peer);
        for (auto const& ep: eps)
          res.emplace_back(infinit::model::Endpoints{ep});
      }
      return res;
    }

    void
    KeyValueStore::mode_run(std::string const& unqualified_name,
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
                            boost::optional<std::string> listen,
                            boost::optional<int> fetch_endpoints_interval,
                            bool no_local_endpoints,
                            bool no_public_endpoints,
                            Strings advertise_host,
                            boost::optional<std::string> grpc,
                            boost::optional<std::string> grpc_port_file)
    {
      ELLE_TRACE_SCOPE("run");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto const name = ifnt.qualified_name(unqualified_name, owner);
      auto kvs = ifnt.key_value_store_get(name);
      auto network = ifnt.network_get(kvs.network, owner);

      if (!grpc)
        elle::err("please specify gRPC endpoint using --grpc");

      network.ensure_allowed(owner, "run", "key-value store");
      cache |= (cache_ram_size || cache_ram_ttl
                || cache_ram_invalidation || cache_disk_size);
      auto const listen_address = boost::optional<boost::asio::ip::address>{};
      auto dht = network.run(
        owner,
        false,
        cache, cache_ram_size, cache_ram_ttl, cache_ram_invalidation,
        async, cache_disk_size, cli.compatibility_version(), {},
        listen_address, {});
      hook_stats_signals(*dht);
      elle::reactor::Thread::unique_ptr dht_grpc_thread;
      auto const eps = model::Endpoints("0.0.0.0", 0);
      int dht_grpc_port = 0;
      dht_grpc_thread.reset(new elle::reactor::Thread("DHT gRPC",
        [dht = dht.get(), ep = *eps.begin(), &dht_grpc_port] {
          infinit::grpc::serve_grpc(*dht, boost::none, ep, &dht_grpc_port);
      }));
      // Wait for DHT gRPC server to be running.
      while (dht_grpc_port == 0)
        elle::reactor::sleep(100_ms);
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
      {
        elle::err("network %s is client only since no storage is attached",
                  name);
      }
      if (dht->local())
      {
        if (endpoints_file)
          endpoints_to_file(dht->local()->server_endpoints(), *endpoints_file);
      }
      auto run = [&, push_p]
        {
          elle::reactor::Thread::unique_ptr poll_thread;
          if (fetch || publish)
          {
            infinit::model::NodeLocations eps;
            network.beyond_fetch_endpoints(eps);
            dht->overlay()->discover(eps);
            if (fetch_endpoints_interval && *fetch_endpoints_interval > 0)
              poll_thread =
                network.make_poll_beyond_thread(*dht, eps,
                                                *fetch_endpoints_interval);
          }
        };
      if (push_p)
        elle::With<InterfacePublisher>(
          ifnt,
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
                elle::reactor::sleep(100_ms);
              port_to_file(kv_grpc_port, *grpc_port_file);
            }));
        }
        static const auto signals = {SIGINT, SIGTERM
#ifndef INFINIT_WINDOWS
                                     , SIGQUIT
#endif
        };
        for (auto s: signals)
        {
          elle::reactor::scheduler().signal_handle(
            s,
            []
            {
              ELLE_DEBUG("stopping key-value store");
              StopServer();
            });
        }
        elle::reactor::background([&] {
          cli.report_action("running", "key-value store", name);
          RunServer(
            go_str(name), go_str(dht_ep), go_str(*grpc), allow_root_creation,
            &kv_grpc_port);
        });
      };
    }
  }
}
