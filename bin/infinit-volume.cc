#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>
#include <elle/system/username.hh>

#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/storage/Storage.hh>

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
      ("name", value<std::string>(), "created volume name")
      ("network", value<std::string>(), "underlying network to use")
      ("mountpoint", value<std::string>(), "where to mount the filesystem")
      ("stdout", "output configuration to stdout")
      ;
    auto help = [&] (std::ostream& output)
      {
        std::cout << "Usage: " << program
                  << " --create --name [name] [options]" << std::endl;
        std::cout << std::endl;
        std::cout << creation_options;
        std::cout << std::endl;
      };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto creation = parse_args(creation_options, args);
    auto name = optional(creation, "name");
    auto network_name = mandatory(creation, "network", help);
    auto mountpoint = mandatory(creation, "mountpoint", help);
    auto network = ifnt.network_get(network_name);
    ELLE_TRACE("start network");
    auto model = network.run();
    ELLE_TRACE("create volume");
    auto fs = elle::make_unique<infinit::filesystem::FileSystem>(model.second);
    infinit::Volume volume(mountpoint, fs->root_address(), network.name);
    {
      if (!creation.count("stdout"))
      {
        if (!name)
          throw elle::Error("volume name unspecified (use --name)");
        ifnt.volume_save(*name, volume);
      }
      else
      {
        elle::serialization::json::SerializerOut s(std::cout, false);
        s.serialize_forward(volume);
      }
    }
  }
  else if (mode.count("run"))
  {
    options_description run_options("Run options");
    run_options.add_options()
      ("name", value<std::string>(), "volume name")
      ("host", value<std::vector<std::string>>()->multitoken(),
       "hosts to connect to")
      ;
    auto help = [&] (std::ostream& output)
      {
        std::cout << "Usage: " << program
                  << " --run --name [name] [options]" << std::endl;
        std::cout << std::endl;
        std::cout << run_options;
        std::cout << std::endl;
      };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto run = parse_args(run_options, args);
    auto name = mandatory(run, "name", "volume name", help);
    std::vector<std::string> hosts;
    if (run.count("host"))
      hosts = run["host"].as<std::vector<std::string>>();
    auto volume = ifnt.volume_get(name);
    auto network = ifnt.network_get(volume.network);
    auto model = network.run(hosts);
    auto fs = volume.run(model.second);
    reactor::wait(*fs);
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
    ("create", "create a new volume")
    ("destroy", "destroy a volume")
    ("list", "list existing volumes")
    ("run", "run volume")
    ;
  options_description options("Infinit volume utility");
  options.add(mode_options);
  return infinit::main(options, &network, argc, argv);
}
