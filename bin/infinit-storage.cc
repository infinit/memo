#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/GoogleDrive.hh>
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
      (name, account.token, std::move(root));
  }
  if (args.count("filesystem"))
  {
    auto path = optional(args, "path");
    if (!path)
      path = (infinit::root_dir() / "blocks" / name).string();
    config =
      elle::make_unique<infinit::storage::FilesystemStorageConfig>
      (name, std::move(*path));
  }
  if (args.count("google"))
  {
    auto root = optional(args, "root");
    if (!root)
      root = name;
    auto account_name = mandatory(args, "account");
    auto account = ifnt.credentials_google(account_name);
    config =
      elle::make_unique<infinit::storage::GoogleDriveStorageConfig>
      (name,
       std::move(root),
       account.refresh_token,
       self_user(ifnt, {}).name);
  }
  if (!config)
    throw CommandLineError("storage type unspecified");
  if (args.count("output"))
  {
    auto output = get_output(args);
    elle::serialization::json::SerializerOut s(*output, false);
    s.serialize_forward(config);
  }
  else
  {
    ifnt.storage_save(name, *config);
    report_created("storage", name);
  }
}

static
void
list(variables_map const& args)
{
  auto s = ifnt.storages_get();
  for (auto const& name: s)
  {
    std::cout << name << std::endl;
  }
}

static
void
export_(variables_map const& args)
{
  auto name = mandatory(args, "name", "storage name");
  std::unique_ptr<infinit::storage::StorageConfig> storage = nullptr;
  try
  {
    storage = ifnt.storage_get(name);
  }
  catch(...)
  {
    storage = ifnt.storage_get(ifnt.qualified_name(name, ifnt.user_get()));
  }
  elle::serialization::json::SerializerOut out(*get_output(args), false);
  out.serialize_forward(storage);
}

int
main(int argc, char** argv)
{
  options_description storage_types("Storage types");
  storage_types.add_options()
    ("dropbox", "store data in a Dropbox")
    ("google", "store data in a Google Drive")
    ("filesystem", "store data on a local filesystem")
    ;
  options_description fs_storage_options("Filesystem storage options");
  fs_storage_options.add_options()
    ("path", value<std::string>(), elle::sprintf(
      "where to store blocks (default: %s)",
      (infinit::root_dir() / "blocks/<name>")).c_str())
    ;
  options_description dropbox_storage_options("Dropbox storage options");
  dropbox_storage_options.add_options()
    ("account", value<std::string>(), "Dropbox account to use")
    ("root", value<std::string>(),
      "where to store blocks in Dropbox (default: .infinit)")
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
        { "name,n", value<std::string>(), "created storage name" },
        option_output("storage"),
      },
      {
        storage_types,
        fs_storage_options,
        dropbox_storage_options,
      },
    },
    {
      "list",
      "List storages",
      &list,
      "",
      {},
    },
    {
      "export",
      "Export storage information",
      &export_,
      "--name STORAGE",
      {
        { "name,n", value<std::string>(), "storage to export" },
        option_output("storage"),
        option_owner,
      }
    },
  };
  return infinit::main("Infinit storage management utility", modes, argc, argv);
}
