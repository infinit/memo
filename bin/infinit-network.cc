#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/overlay/Kalimero.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kademlia/kademlia.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/storage/Strip.hh>

ELLE_LOG_COMPONENT("infinit-network");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

static
void
create(variables_map const& args)
{
  auto name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  std::unique_ptr<infinit::overlay::Configuration> overlay_config;
  if (args.count("stonehenge"))
  {
    auto stonehenge =
      elle::make_unique<infinit::overlay::StonehengeConfiguration>();
    stonehenge->hosts =
      mandatory<std::vector<std::string>>(args, "peer", "stonehenge hosts");
    overlay_config = std::move(stonehenge);
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
        throw elle::Error("'encrypt' must be 'no', 'lazy' or 'yes'");
    }
    else
    {
      kelips->encrypt = false;
      kelips->accept_plain = true;
    }
    if (args.count("protocol"))
    {
      std::string proto = args["protocol"].as<std::string>();
      try
      {
        kelips->rpc_protocol =
          elle::serialization::Serialize<infinit::model::doughnut::Local::Protocol>::convert(proto);
      }
      catch (elle::serialization::Error const& e)
      {
        throw elle::Error("protocol must be one of: utp, tcp, all");
      }
    }
    overlay_config = std::move(kelips);
  }
  if (!overlay_config)
  {
    overlay_config.reset(new infinit::overlay::KalimeroConfiguration());
  }
  overlay_config->join();
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
      storage.reset(
        new infinit::storage::StripStorageConfig(std::move(backends)));
  }
  int replicas = 1;
  if (args.count("replication-factor"))
    replicas = args["replication-factor"].as<int>();
  auto dht =
    elle::make_unique<infinit::model::doughnut::Configuration>(
      std::move(overlay_config),
      owner.keypair(),
      owner.public_key,
      infinit::model::doughnut::Passport(
        owner.public_key,
        ifnt.qualified_name(name, owner),
        owner.private_key.get()),
      owner.name,
      replicas,
      args.count("async"));
  {
    infinit::Network network;
    network.storage = std::move(storage);
    network.model = std::move(dht);
    network.name = ifnt.qualified_name(name, owner);
    if (args.count("port"))
      network.port = args["port"].as<int>();
    bool stdout = args.count("stdout") && args["stdout"].as<bool>();
    if (!stdout)
    {
      ifnt.network_save(network);
      report_created("network", name);
    }
    if (stdout || script_mode)
      elle::serialization::json::serialize(network, std::cout, false);
    if (args.count("push") && args["push"].as<bool>())
    {
      infinit::NetworkDescriptor desc(
        network.name,
        std::move(network.dht()->overlay),
        std::move(network.dht()->owner),
        network.dht()->replicas);
      beyond_push("network", desc.name, desc, owner);
    }
  }
}

static
void
export_(variables_map const& args)
{
  auto owner = self_user(ifnt, args);
  auto output = get_output(args);
  auto network_name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(network_name, owner);
  {
    auto& dht = static_cast<infinit::model::doughnut::Configuration&>
      (*network.model);
    infinit::NetworkDescriptor desc(
      network.name, std::move(dht.overlay), std::move(dht.owner), std::move(dht.replicas));
    elle::serialization::json::serialize(desc, *output, false);
  }
  report_exported(*output, "network", network.name);
}

static
void
fetch(variables_map const& args)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network name");
  network_name = ifnt.qualified_name(network_name, owner);
  auto desc =
    beyond_fetch<infinit::NetworkDescriptor>("network", network_name);
  ifnt.network_save(std::move(desc));
}

static
void
import(variables_map const& args)
{
  auto input = get_input(args);
  auto desc =
    elle::serialization::json::deserialize<infinit::NetworkDescriptor>
    (*input, false);
  ifnt.network_save(desc);
  report_imported("network", desc.name);
}

static
void
invite(variables_map const& args)
{
  auto self = ifnt.user_get(optional(args, option_owner.long_name()));
  auto network_name = mandatory(args, "name", "network name");
  auto user_name = mandatory(args, "user", "user name");
  auto network = ifnt.network_descriptor_get(network_name, self);
  auto user = ifnt.user_get(user_name);
  if (self.public_key != network.owner)
    throw elle::Error(
      elle::sprintf("not owner of network \"%s\"", network_name));
  infinit::model::doughnut::Passport passport(
    user.public_key,
    network.name,
    self.private_key.get());
  bool push = args.count("push") && args["push"].as<bool>();
  if (push)
    beyond_push(
      elle::sprintf("networks/%s/passports/%s", network.name, user_name),
      "passport for",
      user_name,
      passport,
      self);
  if (!push || args.count("output"))
  {
    auto output = get_output(args);
    elle::serialization::json::serialize(passport, *output, false);
    report_action_output(
      *output, "wrote", "passport for", network.name);
  }
}

static
void
join(variables_map const& args)
{
  auto owner = self_user(ifnt, args);
  auto input = get_input(args);
  auto network_name = mandatory(args, "name", "network name");
  auto storage_name = optional(args, "storage");
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  if (storage_name)
    storage = ifnt.storage_get(*storage_name);
  {
    auto desc = ifnt.network_descriptor_get(network_name, owner);
    auto passport = [&] () -> infinit::model::doughnut::Passport
    {
      bool fetch = args.count("fetch") && args["fetch"].as<bool>();
      if (fetch)
        return beyond_fetch<infinit::model::doughnut::Passport>(
            elle::sprintf("networks/%s/passports/%s", network_name, owner.name),
            "passport for",
            network_name);
      if (!args.count("input") && owner.public_key == desc.owner)
      {
        return infinit::model::doughnut::Passport(
            owner.public_key,
            desc.name,
            owner.private_key.get());
      }
      else
      {
        auto input = get_input(args);
        return elle::serialization::json::deserialize
          <infinit::model::doughnut::Passport>(*input, false);
      }
    }();

    bool ok = passport.verify(desc.owner);
    if (!ok)
      throw elle::Error("passport signature is invalid");
    infinit::Network network;
    desc.overlay->join();
    network.model =
      elle::make_unique<infinit::model::doughnut::Configuration>(
        std::move(desc.overlay),
        owner.keypair(),
        std::move(desc.owner),
        std::move(passport),
        owner.name,
        desc.replicas);
    network.storage = std::move(storage);
    network.name = desc.name;
    if (args.count("port"))
      network.port = args["port"].as<int>();
    ifnt.network_save(network, true);
    report_action("joined", "network", network.name);
  }
}

static
void
list(variables_map const& args)
{
  for (auto const& network: ifnt.networks_get())
    std::cout << network.name << std::endl;
}

static
void
push(variables_map const& args)
{
  auto network_name = mandatory(args, "name", "network name");
  auto self = self_user(ifnt, args);
  auto network = ifnt.network_get(network_name, self);
  {
    auto& dht = *network.dht();
    auto owner_uid = infinit::User::uid(dht.owner);
    infinit::NetworkDescriptor desc(
      network.name, std::move(dht.overlay), std::move(dht.owner), std::move(dht.replicas));
    beyond_push("network", desc.name, desc, self);
  }
}

static void
unpush(variables_map const& args)
{
  auto network_name = mandatory(args, "name", "network name");
  auto owner = self_user(ifnt, args);
  beyond_delete("network", network_name, owner);
}

static void
delete_(variables_map const& args)
{
  auto owner = self_user(ifnt, args);
  auto network_name = mandatory(args, "name", "network name");
  auto path = ifnt._network_path(network_name);
  if (boost::filesystem::remove(path))
    report_action("deleted", "network", network_name);
  else
    throw elle::Error(
      elle::sprintf("File for network could not be deleted: %s", path));
}

static
void
run(variables_map const& args)
{
  auto name = mandatory(args, "name", "network name");
  auto self = self_user(ifnt, args);
  auto network = ifnt.network_get(name, self);
  std::vector<std::string> hosts;
  if (args.count("peer"))
    hosts = args["peer"].as<std::vector<std::string>>();
  bool push = args.count("push") && args["push"].as<bool>();
  bool fetch = args.count("fetch") && args["fetch"].as<bool>();
  if (fetch)
    beyond_fetch_endpoints(network, hosts);
  auto local = network.run(hosts, false, false, {}, false,
                           args.count("async") && args["async"].as<bool>(),
                           args.count("cache-model") && args["cache-model"].as<bool>());
  if (!local.first)
    throw elle::Error(elle::sprintf("network \"%s\" is client-only", name));
  reactor::scheduler().signal_handle(
    SIGINT,
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
    elle::With<InterfacePublisher>(
      network, self, local.second->overlay()->node_id(),
      local.first->server_endpoint().port()) << [&]
    {
      run();
    };
  else
    run();
}

static
void
list_storage(variables_map const& args)
{
  std::string name;
  {
    std::string network_name = mandatory(args, "name", "network name");
    infinit::User owner = self_user(ifnt, args);
    name = ifnt.network_path_get(network_name, owner);
  }
  namespace bf = boost::filesystem;
  std::ifstream is(name);
  auto json = boost::any_cast<elle::json::Object>(elle::json::read(is));
  auto storage = boost::any_cast<elle::json::Object>(json["storage"]);
  std::string type = boost::any_cast<std::string>(storage["type"]);
  if (type == "strip")
    for (auto const& b: boost::any_cast<elle::json::Array>(storage["backend"]))
    {
      auto bb = boost::any_cast<elle::json::Object>(b);
      std::string path = boost::any_cast<std::string>(bb["path"]);
      name = bf::path(path).filename().string();
      std::cout << name << "\n";
    }
  else
  {
    std::string path = boost::any_cast<std::string>(storage["path"]);
    name = bf::path(path).filename().string();
    std::cout << name << "\n";
  }
  std::cout << std::endl;
}

static
void
users(variables_map const& args)
{
  std::string network_name = mandatory(args, "name", "network_name");
  auto res =
    beyond_fetch<std::unordered_map<std::string, std::vector<std::string>>>(
      elle::sprintf("networks/%s/users", network_name),
      "users of",
      network_name,
      boost::none,
      false
    );
  for (std::string const& user: res["users"])
    std::cout << user << std::endl;
}

int main(int argc, char** argv)
{
  program = argv[0];

  options_description overlay_types_options("Overlay types");
  overlay_types_options.add_options()
    ("kalimero", "use a kalimero overlay network")
    ("kelips", "use a kelips overlay network")
    ("stonehenge", "use a stonehenge overlay network")
    ("kademlia", "Use a Kademlia overlay network")
    ;
  options_description stonehenge_options("Stonehenge options");
  stonehenge_options.add_options()
    ("peer", value<std::vector<std::string>>()->multitoken(),
     "hosts to connect to")
    ;
  options_description kelips_options("Kelips options");
  kelips_options.add_options()
    ("nodes", value<int>(), "estimate of the total number of nodes")
    ("k", value<int>(), "number of groups")
    ("encrypt", value<std::string>(), "no, lazy or yes")
    ("protocol", value<std::string>(), "RPC protocol to use: tcp,utp,all")
    ;
  options_description options("Infinit network utility");
  Modes modes {
    {
      "create",
      "Create a network",
      &create,
      "--name NAME [OVERLAY-TYPE OVERLAY-OPTIONS...] [STORAGE...]",
      {
        { "name,n", value<std::string>(), "created network name" },
        { "storage,s", value<std::vector<std::string>>()->multitoken(),
            "optional storage to contribute" },
        option_owner,
        { "port,p", value<int>(), "port to listen on (random by default)" },
        { "replication-factor,r", value<int>(), "data replication factor" },
        { "async", bool_switch(), "Use asynchronious operations" },
        { "stdout", bool_switch(), "output configuration to stdout" },
        { "push", bool_switch(),
          elle::sprintf("push the network to %s", beyond()).c_str() },
      },
      {
        overlay_types_options,
        stonehenge_options,
        kelips_options,
      },
    },
    {
      "export",
      "Export a network",
      &export_,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to export" },
        option_owner,
        { "output,o", value<std::string>(),
            "file to write exported network to (defaults to stdout)" },
      },
    },
    {
      "fetch",
      "Fetch a network",
      &fetch,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to fetch" },
        option_owner,
      },
    },
    {
      "import",
      "Import a network",
      &import,
      "",
      {
        { "input,i", value<std::string>(),
            "file to read network from (defaults to stdin)" },
      },
    },
    {
      "invite",
      "Create a passport to a network for a user",
      &invite,
      "--name NETWORK --user USER",
      {
        option_owner,
        { "name,n", value<std::string>(), "network to create the passport to" },
        { "output,o", value<std::string>(),
            "file to write the passport to (defaults to stdout)" },
        { "push,p", bool_switch(),
            elle::sprintf("push the passport to %s", beyond()).c_str() },
        { "user,u", value<std::string>(), "user to create the passport for" },
      },
    },
    {
      "join",
      "Join a network",
      &join,
      "--name NETWORK",
      {
        option_owner,
        { "input,i", value<std::string>(),
            "file to read passport from (defaults to stdin)" },
        { "name,n", value<std::string>(), "network to join" },
        { "fetch,f", bool_switch(),
            elle::sprintf("fetch the passport from %s", beyond()).c_str() },
        { "port", value<int>(), "port to listen on (random by default)" },
        { "storage", value<std::string>(), "optional storage to contribute" },
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
      elle::sprintf("Push a network to %s", beyond()).c_str(),
      &push,
      "--name NETWORK",
      {
        option_owner,
        { "name,n", value<std::string>(), "network to push" }
      },
    },
    {
      "delete",
      "Delete a network",
      &delete_,
      {"--name NETWORK"},
      {
        {"name,n", value<std::string>(), "network to delete"},
        option_owner,
      },
    },
    {
      "unpush",
      elle::sprintf("Remove a network from %s", beyond()).c_str(),
      &unpush,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "volume to remove" },
        option_owner,
      },
    },
    {
      "run",
      "Run a network",
      &run,
      "--name NETWORK",
      {
        option_owner,
        { "fetch", bool_switch(),
            elle::sprintf("fetch endpoints from %s", beyond()).c_str() },
        { "peer", value<std::vector<std::string>>()->multitoken(),
            "peer to connect to (host:port)" },
        { "name", value<std::string>(), "created network name" },
        { "push", bool_switch(),
            elle::sprintf("push endpoints to %s", beyond()).c_str() },
        { "async", bool_switch(), "Use asynchronious operations" },
        { "cache-model", bool_switch(), "Enable model caching"},
      },
    },
    {
      "list-storage",
      "List all contributed storage of a network",
      &list_storage,
      "--name NETWORK",
      {
        option_owner,
        { "name", value<std::string>(), "network name" },
      },
    },
    {
      "members",
      "List all users in a network",
      &users,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network name" },
      },
    },
  };
  return infinit::main("Infinit network management utility", modes, argc, argv);
}
