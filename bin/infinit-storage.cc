#include <algorithm>
#include <cstring>
#include <cctype>

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
int64_t
convert_capacity(int64_t value, std::string quantifier)
{
  if (quantifier == "b" || quantifier == "")
    return value;

  if (quantifier == "kb")
    return value * 1000;
  if (quantifier == "kib")
    return value << 10;

  if (quantifier == "mb")
    return value * 1000000;
  if (quantifier == "mib")
    return value << 20;

  if (quantifier == "gb")
    return value * 1000000000;
  if (quantifier == "gib")
    return value << 30;

  if (quantifier == "tb")
    return value * 1000000000000;
  if (quantifier == "tib")
    return value << 40;

  throw elle::Error(
    elle::sprintf("This format is not supported: %s", quantifier));
}

static
std::string
pretty_print(int64_t bytes, int64_t zeros)
{
  std::string str = std::to_string(bytes);
  std::string integer = std::to_string(bytes / zeros);

  return integer + "." + str.substr(integer.size(), 2);
}

static
std::string
pretty_print(int64_t bytes)
{
  if (bytes / 1000 == 0)
    return std::to_string(bytes) + "B";
  if (bytes / 1000000 == 0) // Under 1 Mio and higher than 1 Kio
    return pretty_print(bytes, 1000) + "KB";
  if (bytes / 1000000000 == 0)
    return pretty_print(bytes, 1000000) + "MB";
  if (bytes / 1000000000000 == 0)
    return pretty_print(bytes, 1000000000) + "GB";
  return pretty_print(bytes, 1000000000000) + "TB";
}

static
int64_t
convert_capacity(std::string value)
{
  std::string quantifier = [&] {
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    std::vector<std::string> to_find = {
      // "b" MUST be the last element.
      "kb", "mb", "gb", "tb", "kib", "mib", "gib", "tib", "b"
    };
    const char* res = nullptr;
    for (auto const& t: to_find)
    {
      res = std::strstr(value.c_str(), t.c_str());
      if (res != nullptr)
        break;
    }

    return res != nullptr ? std::string(res) : std::string("");
  }();
  auto intval = std::stoll(value.substr(0, value.size() - quantifier.size()));
  return convert_capacity(intval, quantifier);
}

COMMAND(create)
{
  auto name = mandatory(args, "name", "storage name");
  auto capacity_repr = option_opt<std::string>(args, "capacity");
  boost::optional<int64_t> capacity;
  if (capacity_repr)
    capacity = convert_capacity(*capacity_repr);
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
      (name, account.token, std::move(root), std::move(capacity));
  }
  if (args.count("filesystem"))
  {
    auto path = optional(args, "path");
    if (!path)
      path = (infinit::root_dir() / "blocks" / name).string();
    config =
      elle::make_unique<infinit::storage::FilesystemStorageConfig>
      (name, std::move(*path), std::move(capacity));
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
       self_user(ifnt, {}).name,
       std::move(capacity));
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
    // Custom message as storage can only be created locally.
    report_action("created", "storage", name);
  }
}

COMMAND(list)
{
  auto storages = ifnt.storages_get();
  for (auto const& storage: storages)
  {
    std::cout << storage->name << ": "
      << (storage->capacity ? pretty_print(*storage->capacity) : "")
      << std::endl;
  }
}

COMMAND(export_)
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

COMMAND(import)
{
  auto input = get_input(args);
  {
    auto storage = elle::serialization::json::deserialize<
      std::unique_ptr<infinit::storage::StorageConfig>>(*input, false);
    if (!storage->name.size())
      throw elle::Error("storage does not have a name");
    ifnt.storage_save(storage->name, *storage);
    report_imported("storage", storage->name);
  }
}

COMMAND(delete_)
{
  auto name = mandatory(args, "name", "storage name");
  auto storage = ifnt.storage_get(name);
  auto path = ifnt._storage_path(name);
  bool ok = boost::filesystem::remove(path);
  if (ok)
    report_action("deleted", "storage", storage->name);
  else
  {
    throw elle::Error(
      elle::sprintf("File for storage could not be deleted: %s", path));
  }
}

int
main(int argc, char** argv)
{
  options_description storage_types("Storage types");
  storage_types.add_options()
    ("filesystem", "store data on a local filesystem")
    ;
  options_description hidden_storage_types("Hidden storage types");
  hidden_storage_types.add_options()
    ("dropbox", "store data in a Dropbox")
    ("google", "store data in a Google Drive")
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
        { "capacity,c", value<std::string>(), "limit the storage capacity, "
          "use: B,kB,kiB,GB,GiB,TB,TiB (optional)" },
        option_output("storage"),
      },
      {
        storage_types,
        fs_storage_options,
      },
      {
      },
      {
        hidden_storage_types,
        dropbox_storage_options,
      }
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
      }
    },
    {
      "import",
      "Import storage information",
      &import,
      "--name STORAGE",
      {
        option_input("storage"),
      }
    },
    {
      "delete",
      "Delete a storage",
      &delete_,
      {},
      {
        { "name,n", value<std::string>(), "storage to delete" },
      },
    },
  };
  return infinit::main("Infinit storage management utility", modes, argc, argv);
}
