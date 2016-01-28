#include <memory>

#include <elle/log.hh>
#include <elle/serialization/json.hh>
#include <elle/json/exceptions.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/overlay/Kalimero.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kademlia/kademlia.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/storage/Strip.hh>

ELLE_LOG_COMPONENT("infinit-network");

#include <main.hh>

infinit::Infinit ifnt;

static
bool
_one(bool seen)
{
  return seen;
}

template <typename First, typename ... Args>
static
bool
_one(bool seen, First&& first, Args&& ... args)
{
  auto b = bool(first);
  if (seen && b)
    return false;
  return _one(seen || b, std::forward<Args>(args)...);
}

template <typename ... Args>
static
bool
one(Args&& ... args)
{
  return _one(false, std::forward<Args>(args)...);
}

COMMAND(create)
{
  auto name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  std::unique_ptr<infinit::overlay::Configuration> overlay_config;
  if (args.count("stonehenge"))
  {
    auto stonehenge =
      elle::make_unique<infinit::overlay::StonehengeConfiguration>();
    ELLE_ABORT("FIXME: stonehenge CLI peer parsing not implemented");
    // stonehenge->peers =
    //   mandatory<std::vector<std::string>>(args, "peer", "stonehenge hosts");
    // overlay_config = std::move(stonehenge);
  }
  if (args.count("kademlia"))
  {
    auto kad = elle::make_unique<infinit::overlay::kademlia::Configuration>();
    overlay_config = std::move(kad);
  }
  if (args.count("kelips"))
  {
    auto kelips =
      elle::make_unique<infinit::overlay::kelips::Configuration>();
    if (args.count("k"))
      kelips->k = args["k"].as<int>();
    else if (args.count("nodes"))
    {
      int nodes = args["nodes"].as<int>();
      if (nodes < 10)
        kelips->k = 1;
      else if (sqrt(nodes) < 5)
        kelips->k = nodes / 5;
      else
        kelips->k = sqrt(nodes);
    }
    else
      kelips->k = 1;
    if (args.count("encrypt"))
    {
      std::string enc = args["encrypt"].as<std::string>();
      if (enc == "no")
      {
        kelips->encrypt = false;
        kelips->accept_plain = true;
      }
      else if (enc == "lazy")
      {
        kelips->encrypt = true;
        kelips->accept_plain = true;
      }
      else if (enc == "yes")
      {
        kelips->encrypt = true;
        kelips->accept_plain = false;
      }
      else
        throw CommandLineError("'encrypt' must be 'no', 'lazy' or 'yes'");
    }
    else
    {
      kelips->encrypt = true;
      kelips->accept_plain = false;
    }
    if (args.count("protocol"))
    {
      std::string proto = args["protocol"].as<std::string>();
      try
      {
        kelips->rpc_protocol =
          elle::serialization::Serialize<
            infinit::model::doughnut::Local::Protocol>::convert(proto);
      }
      catch (elle::serialization::Error const& e)
      {
        throw CommandLineError("protocol must be one of: utp, tcp, all");
      }
    }
    overlay_config = std::move(kelips);
  }
  if (!overlay_config)
  {
    overlay_config.reset(new infinit::overlay::KalimeroConfiguration());
  }
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  auto storage_count = args.count("storage");
  if (storage_count > 0)
  {
    auto storages = args["storage"].as<std::vector<std::string>>();
    std::vector<std::unique_ptr<infinit::storage::StorageConfig>> backends;
    for (auto const& storage: storages)
      backends.emplace_back(ifnt.storage_get(storage));
    if (backends.size() == 1)
      storage = std::move(backends[0]);
    else
    {
      storage.reset(
        new infinit::storage::StripStorageConfig(std::move(backends)));
    }
  }
  // Consensus
  std::unique_ptr<
    infinit::model::doughnut::consensus::Configuration> consensus_config;
  {
    int replication_factor = 1;
    if (args.count("replication-factor"))
      replication_factor = args["replication-factor"].as<int>();
    if (replication_factor < 1)
      throw CommandLineError("replication factor must be greater than 0");
    bool no_consensus = args.count("no-consensus");
    bool paxos = args.count("paxos");
    if (!no_consensus)
      paxos = true;
    if (!one(no_consensus, paxos))
      throw CommandLineError("more than one consensus specified");
    if (paxos)
      consensus_config = elle::make_unique<
        infinit::model::doughnut::consensus::Paxos::Configuration>(
          replication_factor);
    else
    {
      if (replication_factor != 1)
      {
        throw CommandLineError(
          "without consensus, replication factor must be 1");
      }
      consensus_config = elle::make_unique<
        infinit::model::doughnut::consensus::Configuration>();
    }
  }
  boost::optional<int> port;
  if (args.count("port"))
    port = args["port"].as<int>();
  auto dht =
    elle::make_unique<infinit::model::doughnut::Configuration>(
      infinit::model::Address::random(),
      std::move(consensus_config),
      std::move(overlay_config),
      std::move(storage),
      owner.keypair(),
      std::make_shared<infinit::cryptography::rsa::PublicKey>(owner.public_key),
      infinit::model::doughnut::Passport(
        owner.public_key,
        ifnt.qualified_name(name, owner),
        infinit::cryptography::rsa::KeyPair(owner.public_key, owner.private_key.get())),
      owner.name,
      std::move(port),
      version);
  {
    infinit::Network network(std::move(ifnt.qualified_name(name, owner)),
                             std::move(dht));
    if (args.count("output"))
    {
      auto output = get_output(args);
      elle::serialization::json::serialize(network, *output, false);
    }
    else
    {
      ifnt.network_save(network);
      report_created("network", network.name);
    }
    if (aliased_flag(args, {"push-network", "push"}))
    {
      infinit::NetworkDescriptor desc(std::move(network));
      beyond_push("network", desc.name, desc, owner);
    }
  }
}

COMMAND(update)
{
  auto name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  auto network = ifnt.network_get(name, owner);
  auto& dht = *network.dht();
  if (auto port = optional<int>(args, "port"))
    dht.port = port.get();
  if (compatibility_version)
    dht.version = compatibility_version.get();
  if (args.count("output"))
  {
    auto output = get_output(args);
    elle::serialization::json::serialize(network, *output, false);
  }
  else
  {
    ifnt.network_save(network, true);
    report_updated("network", network.name);
  }
  if (aliased_flag(args, {"push-network", "push"}))
  {
    infinit::NetworkDescriptor desc(std::move(network));
    beyond_push("network", desc.name, desc, owner);
  }
}

COMMAND(export_)
{
  auto owner = self_user(ifnt, args);
  auto output = get_output(args);
  auto network_name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(network_name, owner);
  {
    infinit::NetworkDescriptor desc(std::move(network));
    elle::serialization::json::serialize(desc, *output, false);
  }
  report_exported(*output, "network", network.name);
}

COMMAND(fetch)
{
  auto self = self_user(ifnt, args);
  auto network_name_ = optional(args, "name");
  auto save = [&self] (infinit::NetworkDescriptor desc) {
    try
    {
      auto network = ifnt.network_get(desc.name, self, false);
      if (network.model)
      {
        auto* d = dynamic_cast<infinit::model::doughnut::Configuration*>(
          network.model.get()
        );
        infinit::Network updated_network(
          desc.name,
          elle::make_unique<infinit::model::doughnut::Configuration>(
            d->id,
            std::move(desc.consensus),
            std::move(desc.overlay),
            std::move(d->storage),
            self.keypair(),
            std::make_shared<infinit::cryptography::rsa::PublicKey>(desc.owner),
            d->passport,
            self.name,
            d->port,
            desc.version));
        ifnt.network_save(updated_network, true);
      }
      else
      {
        ifnt.network_save(desc, true);
      }
    }
    catch (MissingLocalResource const& e)
    {
      ifnt.network_save(desc);
    }
  };
  if (network_name_)
  {
    std::string network_name = ifnt.qualified_name(network_name_.get(), self);
    save(beyond_fetch<infinit::NetworkDescriptor>("network", network_name));
  }
  else // Fetch all networks for self.
  {
    // FIXME: Workaround for NetworkDescriptor's copy constructor being deleted.
    // Remove when serialization does not require copy.
    auto res = beyond_fetch_json(elle::sprintf("users/%s/networks", self.name),
                                 "networks for user",
                                 self.name,
                                 self);
    auto root = boost::any_cast<elle::json::Object>(res);
    auto networks_vec =
      boost::any_cast<std::vector<elle::json::Json>>(root["networks"]);
    for (auto const& network_json: networks_vec)
    {
      try
      {
        elle::serialization::json::SerializerIn input(network_json, false);
        save(input.deserialize<infinit::NetworkDescriptor>());
      }
      catch (ResourceAlreadyFetched const& error)
      {}
    }
  }
}

COMMAND(import)
{
  auto input = get_input(args);
  auto desc =
    elle::serialization::json::deserialize<infinit::NetworkDescriptor>
    (*input, false);
  ifnt.network_save(desc);
  report_imported("network", desc.name);
}

COMMAND(link_)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network name");
  auto storage_name = optional(args, "storage");
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  if (storage_name)
    storage = ifnt.storage_get(storage_name.get());
  auto desc = [&] () -> infinit::NetworkDescriptor
  {
    try
    {
      return ifnt.network_descriptor_get(network_name, self, false);
    }
    catch (elle::serialization::Error const&)
    {
      throw elle::Error(elle::sprintf(
        "this device has already been linked to %s", network_name));
    }
  }();
  auto passport = [&] () -> infinit::Passport
  {
    if (self.public_key == desc.owner)
    {
      return infinit::Passport(
        self.public_key, desc.name,
        infinit::cryptography::rsa::KeyPair(self.public_key, self.private_key.get()));
    }
    return ifnt.passport_get(desc.name, self.name);
  }();
  bool ok = passport.verify(
    passport.certifier() ? *passport.certifier() : desc.owner);
  if (!ok)
    throw elle::Error("passport signature is invalid");
  if (storage && !passport.allow_storage())
    throw elle::Error("passport does not allow storage");
  infinit::Network network(
    desc.name,
    elle::make_unique<infinit::model::doughnut::Configuration>(
      infinit::model::Address::random(),
      std::move(desc.consensus),
      std::move(desc.overlay),
      std::move(storage),
      self.keypair(),
      std::make_shared<infinit::cryptography::rsa::PublicKey>(desc.owner),
      std::move(passport),
      self.name,
      boost::optional<int>(),
      desc.version));
  ifnt.network_save(network, true);
  report_action("linked", "device to network", network.name);
}

COMMAND(list)
{
  for (auto const& network: ifnt.networks_get())
    std::cout << network.name << std::endl;
}

COMMAND(push)
{
  auto network_name = mandatory(args, "name", "network name");
  auto self = self_user(ifnt, args);
  auto network = ifnt.network_get(network_name, self);
  {
    auto& dht = *network.dht();
    auto owner_uid = infinit::User::uid(*dht.owner);
    infinit::NetworkDescriptor desc(std::move(network));
    beyond_push("network", desc.name, desc, self);
  }
}

COMMAND(pull)
{
  auto name_ = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  auto network_name = ifnt.qualified_name(name_, owner);
  beyond_delete("network", network_name, owner);
}

COMMAND(delete_)
{
  auto name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  auto network_name = ifnt.qualified_name(name, owner);
  auto path = ifnt._network_path(network_name);
  if (boost::filesystem::remove(path))
    report_action("deleted", "network", network_name, std::string("locally"));
  else
    throw elle::Error(
      elle::sprintf("File for network could not be deleted: %s", path));
}

COMMAND(run)
{
  auto name = mandatory(args, "name", "network name");
  auto self = self_user(ifnt, args);
  auto network = ifnt.network_get(name, self);
  infinit::overlay::NodeEndpoints eps;
  if (args.count("peer"))
  {
    auto hosts = args["peer"].as<std::vector<std::string>>();
    for (auto const& h: hosts)
      eps[infinit::model::Address()].push_back(h);
  }
  bool fetch = aliased_flag(args, {"fetch-endpoints", "fetch", "publish"});
  if (fetch)
    beyond_fetch_endpoints(network, eps);
  bool cache = flag(args, option_cache.long_name());
  boost::optional<int> cache_size =
    option_opt<int>(args, option_cache_size.long_name());
  boost::optional<int> cache_ttl =
    option_opt<int>(args, option_cache_ttl.long_name());
  boost::optional<int> cache_invalidation =
    option_opt<int>(args, option_cache_invalidation.long_name());
  if (cache_size || cache_ttl || cache_invalidation)
    cache = true;
  auto dht =
    network.run(eps, false, cache, cache_size, cache_ttl, cache_invalidation,
                flag(args, "async"), compatibility_version);
  // Only push if we have are contributing storage.
  bool push = aliased_flag(args, {"push-endpoints", "push", "publish"})
            && dht->local()->storage();
  if (!dht->local())
    throw elle::Error(elle::sprintf("network \"%s\" is client-only", name));
  static const std::vector<int> signals = {SIGINT, SIGTERM, SIGQUIT};
  for (auto signal: signals)
    reactor::scheduler().signal_handle(
    signal,
    [&]
    {
      ELLE_TRACE("terminating");
      reactor::scheduler().terminate();
    });
  auto run = [&]
    {
      report_action("running", "network", network.name);
      reactor::sleep();
    };
  if (push)
  {
    elle::With<InterfacePublisher>(
      network, self, dht->overlay()->node_id(),
      dht->local()->server_endpoint().port()) << [&]
    {
      run();
    };
  }
  else
    run();
}

COMMAND(list_storage)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(network_name, owner);
  if (network.model->storage)
  {
    if (auto strip = dynamic_cast<infinit::storage::StripStorageConfig*>(
        network.model->storage.get()))
    {
      for (auto const& s: strip->storage)
        std::cout << s->name << "\n";
    }
    else
    {
      std::cout << network.model->storage->name;
    }
    std::cout << std::endl;
  }
}

COMMAND(stats)
{
  auto owner = self_user(ifnt, args);
  std::string network_name = mandatory(args, "name", "network_name");
  std::string name = ifnt.qualified_name(network_name, owner);
  Storages res =
    beyond_fetch<Storages>(
      elle::sprintf("networks/%s/stat", name),
      "stat",
      "stat",
      boost::none,
      Headers{},
      false);

  // FIXME: write Storages::operator(std::ostream&)
  std::cout << "{\"usage\": " << res.usage
         << ", \"capacity\": " << res.capacity
         << "}" << std::endl;
}

int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Mode::OptionsDescription overlay_types_options("Overlay types");
  overlay_types_options.add_options()
    ("kalimero", "use a Kalimero overlay network (default)")
    ("kelips", "use a Kelips overlay network")
    ("stonehenge", "use a Stonehenge overlay network")
    ("kademlia", "use a Kademlia overlay network")
    ;
  Mode::OptionsDescription consensus_types_options("Consensus types");
  consensus_types_options.add_options()
    ("paxos", "use Paxos consensus algorithm (default)")
    ("no-consensus", "use no consensus algorithm")
    ;
  Mode::OptionsDescription stonehenge_options("Stonehenge options");
  stonehenge_options.add_options()
    ("peer", value<std::vector<std::string>>()->multitoken(),
     "hosts to connect to (host:port)")
    ;
  Mode::OptionsDescription kelips_options("Kelips options");
  kelips_options.add_options()
    ("nodes", value<int>(), "estimate of the total number of nodes")
    ("k", value<int>(), "number of groups (default: 1)")
    ("encrypt", value<std::string>(),
      "use encryption: no,lazy,yes (default: yes)")
    ("protocol", value<std::string>(),
      "RPC protocol to use: tcp,utp,all (default: all)")
    ;
  Modes modes {
    {
      "create",
      "Create a network",
      &create,
      "--name NAME "
        "[OVERLAY-TYPE OVERLAY-OPTIONS...] "
        "[CONSENSUS-TYPE CONSENSUS-OPTIONS...] "
        "[--storage STORAGE...]",
      {
        { "name,n", value<std::string>(), "created network name" },
        { "storage", value<std::vector<std::string>>()->multitoken(),
          "storage to contribute (optional)" },
        { "port", value<int>(), "port to listen on (default: random)" },
        { "replication-factor,r", value<int>(),
          "data replication factor (default: 1)" },
        option_output("network"),
        { "push-network", bool_switch(),
          elle::sprintf("push the network to %s", beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-network" },
      },
      {
        consensus_types_options,
        overlay_types_options,
        stonehenge_options,
        kelips_options,
      },
    },
    {
      "update",
      "Update a network",
      &update,
      "--name NAME",
      {
        { "name,n", value<std::string>(), "network to update" },
        { "port", value<int>(), "port to listen on (default: random)" },
        option_output("network"),
        { "push-network", bool_switch(),
            elle::sprintf("push the updated network to %s",
                          beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-network" },
      },
      {},
    },
    {
      "export",
      "Export a network",
      &export_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to export" },
        option_output("network"),
      },
    },
    {
      "fetch",
      elle::sprintf("Fetch a network from %s", beyond(true)).c_str(),
      &fetch,
      {},
      {
        { "name,n", value<std::string>(), "network to fetch (optional)" },
      },
    },
    {
      "import",
      "Import a network",
      &import,
      {},
      {
        option_input("network"),
      },
    },
    {
      "link",
      "Link this device to a network",
      &link_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to link to" },
      },
      {},
      // Hidden options.
      {
        { "storage", value<std::string>(), "storage to contribute (optional)" },
      },

    },
    {
      "list",
      "List networks",
      &list,
      {},
    },
    {
      "push",
      elle::sprintf("Push a network to %s", beyond(true)).c_str(),
      &push,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to push" },
      },
    },
    {
      "delete",
      "Delete a network locally",
      &delete_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to delete" },
      },
    },
    {
      "pull",
      elle::sprintf("Remove a network from %s", beyond(true)).c_str(),
      &pull,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to remove" },
      },
    },
    {
      "run",
      "Run a network",
      &run,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to run" },
        { "peer", value<std::vector<std::string>>()->multitoken(),
          "peer to connect to (host:port)" },
        { "async", bool_switch(), "use asynchronous operations" },
        option_cache,
        option_cache_size,
        option_cache_ttl,
        option_cache_invalidation,
        { "fetch-endpoints", bool_switch(),
          elle::sprintf("fetch endpoints from %s", beyond(true)).c_str() },
        { "fetch,f", bool_switch(), "alias for --fetch-endpoints" },
        { "push-endpoints", bool_switch(),
          elle::sprintf("push endpoints to %s", beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-endpoints" },
        { "publish", bool_switch(),
          "alias for --fetch-endpoints --push-endpoints" },
      },
    },
    {
      "list-storage",
      "List all storage contributed by this device to a network",
      &list_storage,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network name" },
      },
    },
    {
      "stats",
      elle::sprintf(
        "Fetch stats of a network on %s", beyond(true)).c_str(),
      &stats,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network name" },
      },
    },
  };
  return infinit::main("Infinit network management utility", modes, argc, argv);
}
