#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <infinit/storage/Storage.hh>

ELLE_LOG_COMPONENT("8storage");

#include "main.hh"

std::string program;
using boost::program_options::options_description;
options_description options("Options");
options_description modes("Modes");
options_description creation("Creation options");
options_description destruction("Destruction options");
options_description types("Storage types");

options_description storage_filesystem_opt("Filesystem storage options");

using infinit::storage::StorageConfig;
typedef elle::serialization::Hierarchy<StorageConfig> Hierarchy;

infinit::Infinit ifnt;

void
storage(boost::program_options::variables_map vm)
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
      std::cout << "Usage: " << program << " --create --name [name] [type] [options]" << std::endl;
      std::cout << std::endl;
      std::cout << creation;
      std::cout << std::endl;
      std::cout << types;
      std::cout << std::endl;
    };
  auto destroy_help = [&] (std::ostream& output)
    {
      std::cout << "Usage: " << program << " --destroy --name [name]" << std::endl;
      std::cout << std::endl;
      std::cout << destruction;
      std::cout << std::endl;
    };
  if (vm.count("help"))
  {
    if (vm.count("create"))
      create_help(std::cout);
    else
      mode_help(std::cout);
    std::cout << "Infinit v" << INFINIT_VERSION << std::endl;
    throw elle::Exit(0);
  }
  if (vm.count("create"))
  {
    if (!vm.count("name"))
    {
      create_help(std::cerr);
      throw elle::Error("storage name unspecified");
    }
    std::string name = vm["name"].as<std::string>();
    std::function<
      std::unique_ptr<StorageConfig>(elle::serialization::SerializerIn&)> const*
      factory = nullptr;
    for (auto const& t: Hierarchy::_map())
    {
      if (vm.count(t.first))
      {
        if (factory)
          throw elle::Error("multiple storage type specified");
        factory = &t.second;
      }
    }
    if (!factory)
    {
      create_help(std::cerr);
      throw elle::Error("storage type unspecified");
    }
    {
      CommandLineSerializer input(vm);
      auto config = (*factory)(input);
      std::unique_ptr<boost::filesystem::ofstream> output;
      if (!vm.count("stdout"))
      {
        auto dir = ifnt.root_dir() / "storage";
        create_directories(dir);
        auto path = dir / name;
        if (exists(path))
          throw elle::Error(
            elle::sprintf("storage '%s' already exists", name));
        output = elle::make_unique<boost::filesystem::ofstream>(path);
        if (!output->good())
          throw elle::Error(
            elle::sprintf("unable to open '%s' for writing", path));
      }
      elle::serialization::json::SerializerOut s
        (output ? *output : std::cout, false);
      s.serialize_forward(config);
    }
  }
  else if (vm.count("destroy"))
  {
    if (!vm.count("name"))
    {
      destroy_help(std::cerr);
      throw elle::Error("storage name unspecified");
    }
    std::string name = vm["name"].as<std::string>();
    {
      auto dir = ifnt.root_dir() / "storage";
      create_directories(dir);
      auto path = dir / name;
      if (!remove(path))
        throw elle::Error(
          elle::sprintf("storage '%s' does not exist", name));
    }
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
    ("create", "create a new storage")
    ("destroy", "destroy a storage")
    ("list", "list existing storages or storage content")
    ;
  creation.add_options()
    ("name", value<std::string>(), "storage name")
    ("stdout", "output configuration to stdout")
    ;
  destruction.add_options()
    ("name", value<std::string>(), "storage name")
    ("wipe", value<std::string>(), "wipe storage content (default)")
    ("no-wipe", value<std::string>(), "do not wipe storage content")
    ;

  for (auto const& t: Hierarchy::_map())
  {
    auto name = elle::sprintf("--%s", t.first);
    auto desc = elle::sprintf("select a storage of type %s", t.first);
    types.add_options()(t.first.c_str(), desc.c_str());
  }

  storage_filesystem_opt.add_options()
    ("path", value<std::string>(), "where to store block files");

  options.add(modes);
  options.add(creation);
  options.add(types);
  options.add(storage_filesystem_opt);
  return infinit::main(std::move(options), &storage, argc, argv);
}
