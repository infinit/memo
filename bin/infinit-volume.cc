#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/system/username.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
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
      throw elle::Error("volume name unspecified");
    }
    std::string name = vm["name"].as<std::string>();
    if (!vm.count("network"))
    {
      create_help(std::cerr);
      throw elle::Error("network unspecified");
    }
    std::string network_name = vm["network"].as<std::string>();
    if (!vm.count("mountpoint"))
    {
      create_help(std::cerr);
      throw elle::Error("mountpoint unspecified");
    }
    std::string mountpoint = vm["mountpoint"].as<std::string>();
    std::unique_ptr<infinit::model::ModelConfig> model;
    std::unique_ptr<infinit::storage::StorageConfig> storage;
    int port;
    {
      boost::filesystem::ifstream f(infinit_dir() / "networks" / network_name);
      if (!f.good())
        throw elle::Error
          (elle::sprintf("network '%s' does not exist", network_name));
      elle::serialization::json::SerializerIn input(f, false);
      input.serialize("model", model);
      input.serialize("port", port);
      input.serialize("storage", storage);
    }
    ELLE_TRACE("start network");
    infinit::model::doughnut::Local local(storage->make(), port);
    {
      auto& dht_cfg =
        static_cast<infinit::model::doughnut::DoughnutModelConfig&>(*model);
      auto keys_save = std::move(dht_cfg.keys);
      auto name_save = std::move(dht_cfg.name);
      dht_cfg.name.reset();
      local.doughnut() = elle::cast<infinit::model::doughnut::Doughnut>
        ::compiletime(dht_cfg.make());
      dht_cfg.keys = std::move(keys_save);
      dht_cfg.name = std::move(name_save);
    }
    ELLE_TRACE("create volume");
    infinit::filesystem::FileSystem fs(model->make());
    auto root_address = fs.root_address();
    {
      std::unique_ptr<boost::filesystem::ofstream> output;
      if (!vm.count("stdout"))
      {
        auto dir = infinit_dir() / "volumes";
        create_directories(dir);
        auto path = dir / name;
        if (exists(path))
          throw elle::Error(
            elle::sprintf("volume '%s' already exists", name));
        output = elle::make_unique<boost::filesystem::ofstream>(path);
        if (!output->good())
          throw elle::Error(
            elle::sprintf("unable to open '%s' for writing", path));
      }
      elle::serialization::json::SerializerOut s
        (output ? *output : std::cout, false);
      s.serialize("model", model);
      s.serialize("mountpoint", mountpoint);
      s.serialize("root_address", root_address);
      s.serialize("storage", storage);
      s.serialize("port", port);
    }
  }
  else if (vm.count("run"))
  {
    if (!vm.count("name"))
    {
      run_help(std::cerr);
      throw elle::Error("volume name unspecified");
    }
    std::string name = vm["name"].as<std::string>();
    std::string mountpoint;
    std::unique_ptr<infinit::model::ModelConfig> model;
    int port;
    infinit::model::Address root_address;
    std::unique_ptr<infinit::storage::StorageConfig> storage;
    {
      boost::filesystem::ifstream f(infinit_dir() / "volumes" / name);
      if (!f.good())
        throw elle::Error
          (elle::sprintf("volume '%s' does not exist", name));
      elle::serialization::json::SerializerIn s(f, false);
      s.serialize("model", model);
      s.serialize("mountpoint", mountpoint);
      s.serialize("root_address", root_address);
      s.serialize("storage", storage);
      s.serialize("port", port);
    }
    ELLE_TRACE("start network");
    infinit::model::doughnut::Local local(storage->make(), port);
    {
      auto& dht_cfg =
        static_cast<infinit::model::doughnut::DoughnutModelConfig&>(*model);
      auto keys_save = std::move(dht_cfg.keys);
      auto name_save = std::move(dht_cfg.name);
      dht_cfg.name.reset();
      local.doughnut() = elle::cast<infinit::model::doughnut::Doughnut>
        ::compiletime(dht_cfg.make());
      dht_cfg.keys = std::move(keys_save);
      dht_cfg.name = std::move(name_save);
    }
    ELLE_TRACE("start volume");
    auto fs =
      elle::make_unique<infinit::filesystem::FileSystem>
      (root_address, model->make());
    reactor::filesystem::FileSystem driver(std::move(fs), true);
    create_directories(boost::filesystem::path(mountpoint));
    driver.mount(mountpoint, {});
    reactor::wait(driver);
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
    ("name", value<std::string>(), "created volume name")
    ("network", value<std::string>(), "underlying network to use")
    ("mountpoint", value<std::string>(), "where to mount the filesystem")
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
