#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/overlay/Kalimero.hh>
#include <infinit/overlay/Stonehenge.hh>
#include <infinit/overlay/kelips/Kelips.hh>
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
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  std::unique_ptr<infinit::overlay::Configuration> overlay_config;
  if (args.count("stonehenge"))
  {
    auto stonehenge =
      elle::make_unique<infinit::overlay::StonehengeConfiguration>();
    stonehenge->hosts =
      mandatory<std::vector<std::string>>(args, "host", "stonehenge hosts");
    overlay_config = std::move(stonehenge);
  }
  if (args.count("kelips"))
  {
    auto kelips =
      elle::make_unique<infinit::overlay::kelips::Configuration>();
    if (args.count("nodes"))
      kelips->config.k = sqrt(args["nodes"].as<int>());
    kelips->config.node_id = infinit::model::Address::random();
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
      storage.reset(
        new infinit::storage::StripStorageConfig(std::move(backends)));
  }
  auto dht =
    elle::make_unique<infinit::model::doughnut::Configuration>(
      std::move(overlay_config),
      owner.keypair(),
      owner.public_key,
      infinit::model::doughnut::Passport(
        owner.public_key,
        ifnt.qualified_name(name, owner.public_key),
        owner.private_key.get()),
      owner.name);
  {
    infinit::Network network;
    network.storage = std::move(storage);
    network.model = std::move(dht);
    network.name = name;
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
  }
}

static
void
export_(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto output = get_output(args);
  auto network_name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(
    ifnt.qualified_name(network_name, owner.public_key));
  {
    auto& dht = static_cast<infinit::model::doughnut::Configuration&>
      (*network.model);
    infinit::NetworkDescriptor desc(
      network.name, std::move(dht.overlay), std::move(dht.owner));
    elle::serialization::json::serialize(desc, *output, false);
  }
  report_exported(*output, "network", network.name);
}

static
void
fetch(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto network_name = mandatory(args, "name", "network name");
  network_name = ifnt.qualified_name(network_name, owner.public_key);
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
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto network_name = mandatory(args, "name", "network name");
  auto user_name = mandatory(args, "user", "user name");
  auto network = ifnt.network_descriptor_get(
    ifnt.qualified_name(network_name, owner.public_key));
  auto user = ifnt.user_get(user_name);
  auto self = ifnt.user_get();
  if (self.public_key != network.owner)
    throw elle::Error(
      elle::sprintf("not owner of network \"%s\"", network_name));
  infinit::model::doughnut::Passport passport(
    user.public_key,
    network.qualified_name(),
    self.private_key.get());
  auto output = get_output(args);
  elle::serialization::json::serialize(passport, *output, false);
  report_action_output(
    *output, "wrote", "passport for", network.qualified_name());
}

static
void
join(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto input = get_input(args);
  auto name = ifnt.qualified_name(mandatory(args, "name", "network name"),
                                  owner.public_key);
  auto storage_name = optional(args, "storage");
  std::unique_ptr<infinit::storage::StorageConfig> storage;
  if (storage_name)
    storage = ifnt.storage_get(*storage_name);
  {
    auto desc = ifnt.network_descriptor_get(name);
    auto passport = [&] () -> infinit::model::doughnut::Passport
    {
      if (!args.count("input") && owner.public_key == desc.owner)
      {
        return infinit::model::doughnut::Passport(
          owner.public_key,
          desc.qualified_name(),
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
    network.model =
      elle::make_unique<infinit::model::doughnut::Configuration>(
        std::move(desc.overlay),
        owner.keypair(),
        std::move(desc.owner),
        std::move(passport),
        owner.name);
    network.storage = std::move(storage);
    network.name = name;
    if (args.count("port"))
      network.port = args["port"].as<int>();
    ifnt.network_save(network, true);
    report_action("joined", "network", network.name);
  }
}

static
void
publish(variables_map const& args)
{
  auto network_name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(network_name);
  {
    auto& dht = *network.dht();
    auto owner_uid = infinit::User::uid(dht.owner);
    infinit::NetworkDescriptor desc(
      network.name, std::move(dht.overlay), std::move(dht.owner));
    beyond_publish(
      "network",
      elle::sprintf("%s/%s", owner_uid, network_name),
      desc);
  }
}

static
void
run(variables_map const& args)
{
  auto name = mandatory(args, "name", "network name");
  auto network = ifnt.network_get(name);
  std::vector<std::string> hosts;
  if (args.count("host"))
    hosts = args["host"].as<std::vector<std::string>>();
  auto local = network.run(hosts);
  if (!local.first)
    throw elle::Error(elle::sprintf("network \"%s\" is client-only", name));
  report_action("running", "network", network.name);
  reactor::sleep();
}

int main(int argc, char** argv)
{
  program = argv[0];

  options_description overlay_types_options("Overlay types");
  overlay_types_options.add_options()
    ("kalimero", "use a kalimero overlay network")
    ("kelips", "use a kelips overlay network")
    ("stonehenge", "use a stonehenge overlay network")
    ;
  options_description stonehenge_options("Stonehenge options");
  stonehenge_options.add_options()
    ("host", value<std::vector<std::string>>()->multitoken(),
     "hosts to connect to")
    ;
  options_description kelips_options("Kelips options");
  kelips_options.add_options()
    ("nodes", value<int>(), "estimate of the total number of nodes")
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
        { "stdout", bool_switch(), "output configuration to stdout" },
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
        { "name,n", value<std::string>(), "network to create the passport to" },
        { "user,u", value<std::string>(), "user to create the passport for" },
        { "output,o", value<std::string>(),
            "file to write the passport to (defaults to stdout)" },
        option_owner,
      },
    },
    {
      "join",
      "Join a network",
      &join,
      "--name NETWORK",
      {
        { "input,i", value<std::string>(),
            "file to read passport from (defaults to stdin)" },
        { "name,n", value<std::string>(), "network to join" },
        option_owner,
        { "port", value<int>(), "port to listen on (random by default)" },
        { "storage", value<std::string>(), "optional storage to contribute" },
      },
    },
    {
      "publish",
      "Publish a network",
      &publish,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network to publish" }
      },
    },
    {
      "run",
      "Run a network",
      &run,
      "--name NETWORK",
      {
        { "name", value<std::string>(), "created network name" },
        { "host", value<std::vector<std::string>>()->multitoken(),
          "hosts to connect to" },
      },
    },
  };
  return infinit::main("Infinit network management utility", modes, argc, argv);
}
