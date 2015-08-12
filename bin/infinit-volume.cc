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

ELLE_LOG_COMPONENT("infinit-volume");

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
    auto name = mandatory(creation, "name", help);
    auto network_name = mandatory(creation, "network", help);
    auto mountpoint = optional(creation, "mountpoint");
    auto network = ifnt.network_get(network_name);
    ELLE_TRACE("start network");
    auto model = network.run();
    ELLE_TRACE("create volume");
    auto fs = elle::make_unique<infinit::filesystem::FileSystem>(model.second);
    infinit::Volume volume(name, mountpoint, fs->root_address(), network.name);
    {
      if (!creation.count("stdout"))
        ifnt.volume_save(volume);
      else
      {
        elle::serialization::json::SerializerOut s(std::cout, false);
        s.serialize_forward(volume);
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
    auto volume_name = mandatory(exportation, "name", "volume name", help);
    auto output = get_output(exportation);
    auto volume = ifnt.volume_get(volume_name);
    volume.mountpoint.reset();
    {
      elle::serialization::json::SerializerOut s(*output, false);
      s.serialize_forward(volume);
    }
  }
  else if (mode.count("import"))
  {
    options_description import_options("Import options");
    import_options.add_options()
      ("input,i", value<std::string>(), "file to read volume from")
      ("mountpoint,m", value<std::string>(), "where to mount the filesystem")
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
      infinit::Volume volume(s);
      volume.mountpoint = optional(importation, "mountpoint");
      ifnt.volume_save(volume);
    }
  }
  else if (mode.count("run"))
  {
    options_description run_options("Run options");
    run_options.add_options()
      ("name", value<std::string>(), "volume name")
      ("mountpoint,m", value<std::string>(), "where to mount the filesystem")
      ("host", value<std::vector<std::string>>()->multitoken(),
       "hosts to connect to")
      ("cache,c", "enable storage caching")
      ("cache-size,s", value<int>(),
       "maximum storage cache in bytes (implies --cache)")
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
    ELLE_TRACE("run network");
    bool cache = run.count("cache");
    boost::optional<int> cache_size;
    if (run.count("cache-size"))
    {
      cache = true;
      cache_size = run["cache-size"].as<int>();
    }
    auto model = network.run(hosts, true, cache, cache_size);
    ELLE_TRACE("run volume");
    auto fs = volume.run(model.second, optional(run, "mountpoint"));
    ELLE_TRACE("wait");
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
    ("export", "export a network for someone else to import")
    ("import", "import a network")
    ("list", "list existing volumes")
    ("run", "run volume")
    ;
  options_description options("Infinit volume utility");
  options.add(mode_options);
  return infinit::main(options, &network, argc, argv);
}
