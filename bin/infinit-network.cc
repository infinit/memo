#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/system/username.hh>

#include <das/serializer.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/overlay/kelips/Kelips.hh>

ELLE_LOG_COMPONENT("infinit-network");

#include "main.hh"

using namespace boost::program_options;
options_description mode_options("Modes");

infinit::Infinit ifnt;

void
network(boost::program_options::variables_map mode,
        std::vector<std::string> args)
{
  if (mode.count("create"))
  {
    options_description creation_options("Creation options");
    creation_options.add_options()
      ("name", value<std::string>(), "created network name")
      ("user", value<std::string>(), "user to create the network as")
      ("storage", value<std::string>(), "optional storage to contribute")
      ("nodes", value<std::string>(), "Estimate of the total number of nodes")
      ("stdout", "output configuration to stdout")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --create [options]" << std::endl;
      output << std::endl;
      output << creation_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    variables_map creation = parse_args(creation_options, args);
    auto name = mandatory(creation, "name", "network name", help);
    auto owner = ifnt.user_get(optional(creation, "user"));
    auto storage_name = optional(creation, "storage");
    auto nodes = optional(creation, "nodes");
    int k = 1;
    if (nodes)
      k = sqrt(std::stoi(*nodes));
    std::unique_ptr<infinit::storage::StorageConfig> storage;
    if (storage_name)
      storage = ifnt.storage_get(*storage_name);
    auto dht =
      elle::make_unique<infinit::model::doughnut::DoughnutModelConfig>(
        elle::make_unique<infinit::overlay::kelips::Configuration>(),
        owner.keypair(),
        owner.public_key,
        infinit::model::doughnut::Passport
          (owner.public_key, name, owner.private_key.get()),
        owner.name);
    auto& kelips_cfg = static_cast<infinit::overlay::kelips::Configuration&>(*dht->overlay);
    kelips_cfg.config.k = k;
    kelips_cfg.config.node_id = infinit::model::Address::random();
    {
      infinit::Network network;
      network.storage = std::move(storage);
      network.model = std::move(dht);
      network.name = name;
      if (!creation.count("stdout"))
      {
        ifnt.network_save(name, network);
      }
      else
      {
        elle::serialization::json::SerializerOut s(std::cout, false);
        s.serialize_forward(network);
      }
    }
  }
  else if (mode.count("export"))
  {
    options_description export_options("Export options");
    export_options.add_options()
      ("name,n", value<std::string>(), "network to export")
      ("output,o", value<std::string>(), "file to write user to "
                                        "(stdout by default)")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --export [options]" << std::endl;
      output << std::endl;
      output << export_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto exportation = parse_args(export_options, args);
    auto network_name = mandatory(exportation, "name", "network name", help);
    auto output = get_output(exportation);
    auto network = ifnt.network_get(network_name);
    {
      auto& dht = static_cast<infinit::model::doughnut::DoughnutModelConfig&>
        (*network.model);
      infinit::NetworkDescriptor desc(
        network.name, std::move(dht.overlay), std::move(dht.owner));
      elle::serialization::json::SerializerOut s(*output, false);
      s.serialize_forward(desc);
    }
  }
  else if (mode.count("import"))
  {
    options_description import_options("Import options");
    import_options.add_options()
      ("input,i", value<std::string>(), "file to read network from")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --import [options]" << std::endl;
      output << std::endl;
      output << import_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto importation = parse_args(import_options, args);
    auto input = get_input(importation);
    {
      elle::serialization::json::SerializerIn s(*input, false);
      infinit::NetworkDescriptor desc(s);
      ifnt.network_save(desc.name, desc);
    }
  }
  else if (mode.count("join"))
  {
    options_description join_options("Join options");
    join_options.add_options()
      ("name,n", value<std::string>(), "network to join")
      ("input,i", value<std::string>(),
       "file to read passport from (defaults to stdin)")
      ("storage", value<std::string>(), "optional storage to contribute")
      ("user,u", value<std::string>(),
       "user to join the network as (defaults to system user)")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --join [options]" << std::endl;
      output << std::endl;
      output << join_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto join = parse_args(join_options, args);
    auto input = get_input(join);
    auto name = mandatory(join, "name", "network name", help);
    auto username = get_username(join, "user");
    auto storage_name = optional(join, "storage");
    std::unique_ptr<infinit::storage::StorageConfig> storage;
    if (storage_name)
      storage = ifnt.storage_get(*storage_name);
    {
      auto desc = ifnt.network_descriptor_get(name);
      auto input = get_input(join);
      elle::serialization::json::SerializerIn passport_s(*input, false);
      infinit::model::doughnut::Passport passport(passport_s);
      bool ok = passport.verify(desc.owner);
      if (!ok)
        throw elle::Error("passport signature is invalid");
      auto user = ifnt.user_get(username);
      infinit::Network network;
      network.model =
        elle::make_unique<infinit::model::doughnut::DoughnutModelConfig>(
          std::move(desc.overlay),
          user.keypair(),
          std::move(desc.owner),
          std::move(passport),
          user.name);
      network.storage = std::move(storage);
      network.name = name;
      remove(ifnt._network_path(name));
      ifnt.network_save(name, network);
    }
  }
  else if (mode.count("run"))
  {
    options_description run_options("Run options");
    run_options.add_options()
      ("name", value<std::string>(), "created network name")
      ("host", value<std::vector<std::string>>()->multitoken(),
       "hosts to connect to")
      ;
    auto help = [&] (std::ostream& output)
      {
        output << "Usage: " << program
        << " --run --name [name] [options]" << std::endl;
        output << std::endl;
        output << run_options;
        output << std::endl;
      };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    variables_map run = parse_args(run_options, args);
    auto name = mandatory(run, "name", "network name", help);
    auto network = ifnt.network_get(name);
    std::vector<std::string> hosts;
    if (run.count("host"))
      hosts = run["host"].as<std::vector<std::string>>();
    auto local = network.run(hosts);
    if (!local.first)
      throw elle::Error(elle::sprintf("network \"%s\" is client-only", name));
    reactor::sleep();
  }
  else if (mode.count("invite"))
  {
    options_description invite_options("Invitation options");
    invite_options.add_options()
      ("name,n", value<std::string>(), "network to create the passport to")
      ("user,u", value<std::string>(), "user to create the passport for")
      ;
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --invite [options]" << std::endl;
      output << std::endl;
      output << invite_options;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto invitation = parse_args(invite_options, args);
    auto network_name = mandatory(invitation, "name", "network name", help);
    auto user_name = mandatory(invitation, "user", "user name", help);
    auto network = ifnt.network_get(network_name);
    auto user = ifnt.user_get(user_name);
    auto self = ifnt.user_get();
    if (self.public_key !=
        static_cast<infinit::model::doughnut::DoughnutModelConfig&>
        (*network.model).owner)
      throw elle::Error(
        elle::sprintf("not owner of network \"%s\"", network_name));
    infinit::model::doughnut::Passport passport(
      user.public_key,
      network_name,
      self.private_key.get());
    auto output = get_output(invitation);
    elle::serialization::json::SerializerOut s(*output, false);
    s.serialize_forward(passport);
  }
  else
  {
    std::cerr << "Usage: " << program << " [mode] [mode-options]" << std::endl;
    std::cerr << std::endl;
    std::cerr << mode_options;
    std::cerr << std::endl;
    throw elle::Error("mode unspecified");
  }
}

int main(int argc, char** argv)
{
  program = argv[0];
  mode_options.add_options()
    ("create", "create a new network")
    // ("destroy", "destroy a network")
    ("export", "export a network for someone else to import")
    ("import", "import a network")
    ("invite", "create a passport to a network for a user")
    ("join", "join a network with a passport")
    // ("list", "list existing networks")
    ("run", "run network")
    ;
  options_description options("Infinit network utility");
  options.add(mode_options);
  return infinit::main(options, &network, argc, argv);
}
