#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/system/username.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/overlay/Stonehenge.hh>

ELLE_LOG_COMPONENT("8network");

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
      ("storage", value<std::string>(), "underlying storage to use")
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
    auto name = optional(creation, "name");
    auto storage = ifnt.storage_get(mandatory(creation, "storage", help));
    auto dht =
      elle::make_unique<infinit::model::doughnut::DoughnutModelConfig>();
    {
      auto overlay =
        elle::make_unique<infinit::overlay::StonehengeOverlayConfig>();
      overlay->nodes.push_back("127.0.0.1:4242");
      dht->overlay = std::move(overlay);
    }
    dht->keys =
      elle::make_unique<infinit::cryptography::rsa::KeyPair>
      (infinit::cryptography::rsa::keypair::generate(2048));
    dht->name = elle::system::username();
    {
      infinit::Network network;
      network.storage = std::move(storage);
      network.port = 4242;
      network.model = std::move(dht);
      if (!creation.count("stdout"))
      {
        if (!name)
          throw elle::Error("network name unspecified (use --name)");
        ifnt.network_save(*name, network);
      }
      else
      {
        elle::serialization::json::SerializerOut s(std::cout, false);
        s.serialize_forward(network);
      }
    }
  }
  else if (mode.count("run"))
  {
    options_description run_options("Run options");
    run_options.add_options()
      ("name", value<std::string>(), "created network name")
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
    auto local = network.run();
    reactor::sleep();
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
    ("destroy", "destroy a network")
    ("list", "list existing networks")
    ("run", "run network")
    ;
  options_description options("Infinit network utility");
  options.add(mode_options);
  return infinit::main(options, &network, argc, argv);
}
