#include <algorithm>
#include <cstring>
#include <cctype>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/log.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/serialization/json.hh>

#include <aws/Credentials.hh>

#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/GCS.hh>
#include <infinit/storage/GoogleDrive.hh>
#include <infinit/storage/S3.hh>
#ifndef INFINIT_WINDOWS
#include <infinit/storage/sftp.hh>
#endif

ELLE_LOG_COMPONENT("infinit-storage");

#include <main.hh>

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
  auto capacity_repr = optional(args, "capacity");
  boost::optional<int64_t> capacity;
  if (capacity_repr)
    capacity = convert_capacity(*capacity_repr);
  std::unique_ptr<infinit::storage::StorageConfig> config;
  if (args.count("filesystem"))
  {
    auto path = optional(args, "path");
    if (!path)
      path = (infinit::xdg_data_home() / "blocks" / name).string();
    else
      path = infinit::canonical_folder(*path).string();
    config =
      elle::make_unique<infinit::storage::FilesystemStorageConfig>
        (name, std::move(*path), std::move(capacity));
  }
  else if (args.count("gcs"))
  {
    auto root = optional(args, "path");
    if (!root)
      root = elle::sprintf("%s_blocks", name);
    auto bucket = mandatory(args, "bucket");
    auto account_name = mandatory(args, "account");
    auto account = ifnt.credentials_gcs(account_name);
    config =
      elle::make_unique<infinit::storage::GCSConfig>
      (name, bucket, *root, self_user(ifnt, {}).name, account->refresh_token,
       std::move(capacity));
  }
  else if (args.count("s3"))
  {
    auto region = mandatory(args, "region", "AWS region");
    auto bucket = mandatory(args, "bucket", "S3 bucket");
    auto account_name = mandatory(args, "account", "AWS account");
    auto root = optional(args, "path");
    if (!root)
      root = elle::sprintf("%s_blocks", name);
    auto account = ifnt.credentials_aws(account_name);
    aws::Credentials aws_credentials(account->access_key_id,
                                     account->secret_access_key,
                                     region, bucket, root.get());
    auto storage_class_str = optional(args, "storage-class");
    aws::S3::StorageClass storage_class = aws::S3::StorageClass::Default;
    if (storage_class_str)
    {
      std::string storage_class_parse = storage_class_str.get();
      std::transform(storage_class_parse.begin(),
                     storage_class_parse.end(),
                     storage_class_parse.begin(),
                     ::tolower);
      if (storage_class_parse == "standard")
        storage_class = aws::S3::StorageClass::Standard;
      else if (storage_class_parse == "standard_ia")
        storage_class = aws::S3::StorageClass::StandardIA;
      else if (storage_class_parse == "reduced_redundancy")
        storage_class = aws::S3::StorageClass::ReducedRedundancy;
      else
      {
        throw CommandLineError(elle::sprintf("unrecognized storage class: %s",
                                             storage_class_str.get()));
      }
    }
    config = elle::make_unique<infinit::storage::S3StorageConfig>(
      name,
      std::move(aws_credentials),
      storage_class,
      std::move(capacity));
  }
  else if (args.count("dropbox"))
  {
    auto root = optional(args, "path");
    if (!root)
      root = elle::sprintf("storage_%s", name);
    auto account_name = mandatory(args, "account", "Dropbox account");
    auto account = ifnt.credentials_dropbox(account_name);
    config =
      elle::make_unique<infinit::storage::DropboxStorageConfig>
      (name, account->token, std::move(root), std::move(capacity));
  }
  else if (args.count("google-drive"))
  {
    auto root = optional(args, "path");
    if (!root)
      root = elle::sprintf(".infinit_%s", name);
    auto account_name = mandatory(args, "account", "Google account");
    auto account = ifnt.credentials_google(account_name);
    config =
      elle::make_unique<infinit::storage::GoogleDriveStorageConfig>
      (name,
       std::move(root),
       account->refresh_token,
       self_user(ifnt, {}).name,
       std::move(capacity));
  }
#ifndef INFINIT_WINDOWS
  else if (args.count("ssh"))
  {
    auto host = mandatory(args, "host", "SSH remote host");
    auto path = mandatory(args, "path", "Remote path to store into");
    config = elle::make_unique<infinit::storage::SFTPStorageConfig>(
      name, host, path, capacity);
  }
#endif
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
    ifnt.storage_save(name, config);
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
              << (storage->capacity ? pretty_print(storage->capacity.get())
                                    : "unlimited")
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
    ifnt.storage_save(storage->name, storage);
    report_imported("storage", storage->name);
  }
}

COMMAND(delete_)
{
  auto name = mandatory(args, "name", "storage name");
  auto storage = ifnt.storage_get(name);
  bool clear = flag(args, "clear-content");
  if (auto fs_storage =
        dynamic_cast<infinit::storage::FilesystemStorageConfig*>(storage.get()))
  {
    if (clear)
    {
      try
      {
        boost::filesystem::remove_all(fs_storage->path);
        report_action("cleared", "storage", fs_storage->name);
      }
      catch (boost::filesystem::filesystem_error const& e)
      {
        throw elle::Error(elle::sprintf("unable to clear storage contents: %s",
                                        e.what()));
      }
    }
  }
  else if (clear)
    throw elle::Error("only filesystem storages can be cleared");
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
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  infinit::check_broken_locale();
  Mode::OptionsDescription storage_types("Storage types");
  storage_types.add_options()
    ("filesystem", "store data on a local filesystem")
    ("gcs", "store data using Google Cloud Storage")
    ("s3", "store data using Amazon S3")
    ;
  Mode::OptionsDescription hidden_storage_types("Hidden storage types");
  hidden_storage_types.add_options()
    ("dropbox", "store data in a Dropbox")
    ("google-drive", "store data in a Google Drive")
    ("ssh", "store data over SSH")
    ;
  Mode::OptionsDescription cloud_options("Cloud storage options");
  cloud_options.add_options()
    ("account", value<std::string>(), "account name to use")
    ("bucket", value<std::string>(), "bucket to use")
  ;
  Mode::OptionsDescription s3_options("Amazon S3 storage options");
  s3_options.add_options()
    ("storage-class", value<std::string>(), "storage class to use: "
       "STANDARD, STANDARD_IA, REDUCED_REDUNDANCY (default: bucket default)")
    ("region", value<std::string>(), "AWS region to use")
  ;
  Mode::OptionsDescription ssh_storage_options("SSH storage options");
  ssh_storage_options.add_options()
    ("host", value<std::string>(), "hostname to connect to")
    ;
  std::string default_locations = elle::sprintf(
    "folder where blocks are stored (default:"
    "\n  filesystem: %s"
    "\n  GCS: <name>_blocks"
    "\n  S3: <name>_blocks"
    // "\n  Dropbox: storage_<name>"
    // "\n  Google Drive: .infinit_<name>"
    ")", (infinit::xdg_data_home() / "blocks/<name>"));
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
        { "path", value<std::string>(), default_locations },
      },
      {
        storage_types,
        cloud_options,
        s3_options,
        // ssh_storage_options,
      },
      {},
      {
        hidden_storage_types,
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
        { "clear-content", bool_switch(),
          "remove all blocks (filesystem only)" },
      },
    },
  };
  return infinit::main("Infinit storage management utility", modes, argc, argv);
}
