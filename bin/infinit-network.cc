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

std::string program;
using boost::program_options::options_description;
options_description options("Options");
options_description modes("Modes");
options_description creation("Creation options");
options_description run("Run options");

infinit::Infinit ifnt;

void
network(boost::program_options::variables_map vm)
{
  auto mode_help = [&] (std::ostream& output)
    {
      output << "Usage: " << program << " [mode] [mode-options]" << std::endl;
      output << std::endl;
      output << modes;
      output << std::endl;
    };
  auto create_help = [&] (std::ostream& output)
    {
      std::cout << "Usage: " << program << " --create --name [name] [options]" << std::endl;
      std::cout << std::endl;
      std::cout << creation;
      std::cout << std::endl;
    };
  auto run_help = [&] (std::ostream& output)
    {
      std::cout << "Usage: " << program << " --run --name [name] [options]" << std::endl;
      std::cout << std::endl;
      std::cout << run;
      std::cout << std::endl;
    };
  if (vm.count("create"))
  {
    auto name = mandatory(vm, "name", "network name", create_help);
    auto storage_name = mandatory(vm, "storage", create_help);
    std::unique_ptr<infinit::storage::StorageConfig> storage;
    {
      boost::filesystem::ifstream f(ifnt.root_dir() / "storage" / storage_name);
      if (!f.good())
        throw elle::Error
          (elle::sprintf("storage '%s' does not exist", storage_name));
      elle::serialization::json::SerializerIn input(f, false);
      storage =
        input.deserialize<std::unique_ptr<infinit::storage::StorageConfig>>();
    }
    auto overlay =
      elle::make_unique<infinit::overlay::StonehengeOverlayConfig>();
    overlay->nodes.push_back("127.0.0.1:4242");
    auto dht =
      elle::make_unique<infinit::model::doughnut::DoughnutModelConfig>();
    dht->overlay = std::move(overlay);
    dht->keys =
      elle::make_unique<infinit::cryptography::rsa::KeyPair>
      (infinit::cryptography::rsa::keypair::generate(2048));
    dht->name = elle::system::username();
    {
      infinit::Network network;
      network.storage = std::move(storage);
      network.port = 4242;
      network.model = std::move(dht);
      if (!vm.count("stdout"))
        ifnt.network_save(name, network);
      else
      {
        elle::serialization::json::SerializerOut s(std::cout, false);
        s.serialize_forward(network);
      }
    }
  }
  else if (vm.count("run"))
  {
    auto name = mandatory(vm, "name", "network name", run_help);
    auto network = ifnt.network_get(name);
    auto local = network.run();
    reactor::sleep();
  }
  else
  {
    mode_help(std::cerr);
    throw elle::Error("mode unspecified");
  }
}

int main(int argc, char** argv)
{
  program = argv[0];

  using boost::program_options::value;
  modes.add_options()
    ("create", "create a new network")
    ("destroy", "destroy a network")
    ("list", "list existing networks")
    ("run", "run network")
    ;
  creation.add_options()
    ("name", value<std::string>(), "created network name")
    ("storage", value<std::string>(), "underlying storage to use")
    ("stdout", "output configuration to stdout")
    ;
  run.add_options()
    ("name", value<std::string>(), "created network name")
    ;

  options.add(modes);
  options.add(creation);
  // options.add(run);
  return infinit::main(std::move(options), &network, argc, argv);
}
