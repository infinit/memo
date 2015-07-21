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
    if (!vm.count("name"))
    {
      create_help(std::cerr);
      throw elle::Error("network name unspecified");
    }
    std::string name = vm["name"].as<std::string>();
    if (!vm.count("storage"))
    {
      create_help(std::cerr);
      throw elle::Error("storage unspecified");
    }
    std::string storage_name = vm["storage"].as<std::string>();
    std::unique_ptr<infinit::storage::StorageConfig> storage;
    {
      boost::filesystem::ifstream f(ifnt.root_dir() / "storage" / storage_name);
      if (!f.good())
        throw elle::Error
          (elle::sprintf("storage '%s' does not exist", storage));
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
      std::unique_ptr<boost::filesystem::ofstream> output;
      if (!vm.count("stdout"))
      {
        auto dir = ifnt.root_dir() / "networks";
        create_directories(dir);
        auto path = dir / name;
        if (exists(path))
          throw elle::Error(
            elle::sprintf("network '%s' already exists", name));
        output = elle::make_unique<boost::filesystem::ofstream>(path);
        if (!output->good())
          throw elle::Error(
            elle::sprintf("unable to open '%s' for writing", path));
      }
      elle::serialization::json::SerializerOut s
        (output ? *output : std::cout, false);
      std::unique_ptr<infinit::model::ModelConfig> model(std::move(dht));
      s.serialize("model", model);
      s.serialize("port", 4242);
      s.serialize("storage", storage);
    }
  }
  else if (vm.count("run"))
  {
    if (!vm.count("name"))
    {
      run_help(std::cerr);
      throw elle::Error("network name unspecified");
    }
    std::string name = vm["name"].as<std::string>();
    auto network = ifnt.network(name);
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
