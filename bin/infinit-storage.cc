#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <reactor/http/Request.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Storage.hh>

ELLE_LOG_COMPONENT("8storage");

#include "main.hh"

using namespace boost::program_options;
options_description mode_options("Modes");

infinit::Infinit ifnt;

void
storage(boost::program_options::variables_map mode,
        std::vector<std::string> args)
{
  if (mode.count("create"))
  {
    options_description creation_options("Creation options");
    creation_options.add_options()
      ("name", value<std::string>(), "storage name")
      ("stdout", "output configuration to stdout")
      ;
    options_description types("Storage types");
    types.add_options()
      ("dropbox", "store data in a Dropbox account")
      ("filesystem", "store files on a local disk")
      ;
    options_description merge;
    merge.add(creation_options);
    merge.add(types);
    options_description fs_storage_options("Filesystem storage options");
    fs_storage_options.add_options()
      ("path", value<std::string>(), "where to store blocks")
      ;
    merge.add(fs_storage_options);
    options_description dropbox_storage_options("Dropbox storage options");
    dropbox_storage_options.add_options()
      ("account", value<std::string>(), "dropbox account to use")
      ("root", value<std::string>(),
       "where to store blocks in dropbox (defaults to .infinit)")
      ("token", value<std::string>(), "authentication token")
      ;
    merge.add(dropbox_storage_options);
    variables_map creation = parse_args(merge, args);
    auto help = [&] (std::ostream& output)
    {
      output << "Usage: " << program
             << " --create [options] [storage-type] [storage-options]"
             << std::endl;
      output << std::endl;
      output << creation_options;
      output << std::endl;
      if (creation.count("filesystem"))
        output << fs_storage_options;
      else if (creation.count("dropbox"))
      {
        output << dropbox_storage_options;
        output << std::endl;
      }
      else
        output << types;
      output << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    auto name = optional(creation, "name");
    std::unique_ptr<infinit::storage::StorageConfig> config;
    if (creation.count("dropbox"))
    {
      auto root = optional(creation, "root");
      auto account_name = mandatory(creation, "account", "account", help);
      auto account = ifnt.credentials_dropbox(account_name);
      config =
        elle::make_unique<infinit::storage::DropboxStorageConfig>
        (account.token, std::move(root));
    }
    if (creation.count("filesystem"))
    {
      auto path = mandatory(creation, "path", "path", help);
      config =
        elle::make_unique<infinit::storage::FilesystemStorageConfig>
        (std::move(path));
    }
    if (!creation.count("stdout"))
    {
      if (!name)
        throw elle::Error("storage name unspecified (use --name)");
        ifnt.storage_save(*name, *config);
    }
    else
    {
      elle::serialization::json::SerializerOut s(std::cout, false);
      s.serialize_forward(config);
    }
  }
  else if (mode.count("destroy"))
  {
    options_description destruction_options("Destruction options");
    destruction_options.add_options()
      ("name", value<std::string>(), "storage name")
      ("wipe", value<std::string>(), "wipe storage content (default)")
      ("no-wipe", value<std::string>(), "do not wipe storage content")
      ;
    auto help = [&] (std::ostream& output)
    {
      std::cout << "Usage: " << program
                << " --destroy --name [name] [options]" << std::endl;
      std::cout << std::endl;
      std::cout << destruction_options;
      std::cout << std::endl;
    };
    if (mode.count("help"))
    {
      help(std::cout);
      throw elle::Exit(0);
    }
    variables_map destruction = parse_args(destruction_options, args);
    auto name = mandatory(destruction, "name", "storage name", help);
    ifnt.storage_remove(name);
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

int
main(int argc, char** argv)
{
  program = argv[0];
  mode_options.add_options()
    ("create", "create a new storage")
    ("destroy", "destroy a storage")
    ("list", "list existing storages or storage content")
    ;
  options_description options("Infinit storage utility");
  options.add(mode_options);
  return infinit::main(options, &storage, argc, argv);
}
