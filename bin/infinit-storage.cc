#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/Storage.hh>

ELLE_LOG_COMPONENT("infinit-storage");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

static
void
create(variables_map const& args)
{
  auto name = mandatory(args, "name", "storage name");
  std::unique_ptr<infinit::storage::StorageConfig> config;
  if (args.count("dropbox"))
  {
    auto root = optional(args, "root");
    if (!root)
      root = name;
    auto account_name = mandatory(args, "account");
    auto account = ifnt.credentials_dropbox(account_name);
    config =
      elle::make_unique<infinit::storage::DropboxStorageConfig>
      (account.token, std::move(root));
  }
  if (args.count("filesystem"))
  {
    auto path = optional(args, "path");
    if (!path)
      path = (infinit::root_dir() / "blocks" / name).string();
    config =
      elle::make_unique<infinit::storage::FilesystemStorageConfig>
      (std::move(*path));
  }
  if (!config)
      throw CommandLineError("storage type unspecified");
  if (!args.count("stdout") || !args["stdout"].as<bool>())
  {
    ifnt.storage_save(name, *config);
    report_created("storage", name);
  }
  else
  {
    elle::serialization::json::SerializerOut s(std::cout, false);
    s.serialize_forward(config);
  }
}

int
main(int argc, char** argv)
{
  options_description storage_types("Storage types");
  storage_types.add_options()
    ("dropbox", "store data in a Dropbox account")
    ("filesystem", "store files on a local filesystem")
    ;
  options_description fs_storage_options("Filesystem storage options");
  fs_storage_options.add_options()
    ("path", value<std::string>(), "where to store blocks")
    ;
  options_description dropbox_storage_options("Dropbox storage options");
  dropbox_storage_options.add_options()
    ("account", value<std::string>(), "dropbox account to use")
    ("root", value<std::string>(),
     "where to store blocks in dropbox (defaults to .infinit)")
    ("token", value<std::string>(), "authentication token")
    ;
  program = argv[0];
  Modes modes {
    {
      "create",
      "Create a storage",
      &create,
      "STORAGE-TYPE [STORAGE-OPTIONS...]",
      {
        { "name", value<std::string>(), "storage name" },
        { "stdout", bool_switch(), "output configuration to stdout" },
      },
      {
        storage_types,
        fs_storage_options,
        dropbox_storage_options,
      },
    },
  };
  return infinit::main("Infinit storage management utility", modes, argc, argv);
}
