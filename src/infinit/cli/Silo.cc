#include <infinit/cli/Silo.hh>

#include <iostream>

#include <boost/algorithm/string/case_conv.hpp>

#include <elle/print.hh>

#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/GCS.hh>
#include <infinit/storage/GoogleDrive.hh>
#include <infinit/storage/S3.hh>
#ifndef INFINIT_WINDOWS
# include <infinit/storage/sftp.hh>
#endif

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>

ELLE_LOG_COMPONENT("cli.block");

namespace infinit
{
  namespace cli
  {
    namespace
    {
      int64_t
      convert_capacity(int64_t value, std::string const& quantifier)
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
        elle::err("invalid capacity: %s", quantifier);
      }

      int64_t
      convert_capacity(std::string value)
      {
        std::string quantifier = [&] {
          boost::algorithm::to_lower(value);
          auto to_find = std::vector<std::string>{
            // "b" MUST be the last element.
            "kb", "mb", "gb", "tb", "kib", "mib", "gib", "tib", "b"
          };
          const char* res = nullptr;
          for (auto const& t: to_find)
            if (res = std::strstr(value.c_str(), t.c_str()))
              break;
          return res ? res : "";
        }();
        auto intval =
          std::stoll(value.substr(0, value.size() - quantifier.size()));
        return convert_capacity(intval, quantifier);
      }

      std::string
      pretty_print(int64_t bytes, int64_t zeros)
      {
        std::string str = std::to_string(bytes);
        std::string integer = std::to_string(bytes / zeros);
        return integer + "." + str.substr(integer.size(), 2);
      }

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
    }

    Silo::Silo(Infinit& infinit)
      : Object(infinit)
      , create(
        "Create local silo",
        das::cli::Options{
          {"path", das::cli::Option{'\0', "directory where to store blocks", false}},
        },
        this->bind(modes::mode_create,
                   name,
                   description = boost::none,
                   capacity = boost::none,
                   output = boost::none,
                   dropbox,
                   filesystem,
                   gcs,
                   google_drive,
                   ssh,
                   s3,
                   account = boost::none,
                   bucket = boost::none,
                   endpoint = "amazonaws.com",
                   host = boost::none,
                   region = boost::none,
                   path = boost::none,
                   storage_class = boost::none))
      , delete_("Delete local silo",
                das::cli::Options(),
                this->bind(modes::mode_delete,
                           name,
                           clear_content = false,
                           purge = false))
      , export_("Export local silo",
                das::cli::Options(),
                this->bind(modes::mode_export,
                           name,
                           output = boost::none))
      , import("Import local silo",
               das::cli::Options(),
               this->bind(modes::mode_import,
                          input = boost::none))
      , list("List local silos",
             das::cli::Options(),
             this->bind(modes::mode_list))
    {}

    void
    Silo::mode_create(std::string name,
                      boost::optional<std::string> description,
                      boost::optional<std::string> capacity_repr,
                      boost::optional<std::string> output,
                      bool dropbox,
                      bool filesystem,
                      bool gcs,
                      bool google_drive,
                      bool ssh,
                      bool s3,
                      boost::optional<std::string> account_name,
                      boost::optional<std::string> bucket,
                      std::string endpoint,
                      boost::optional<std::string> host,
                      boost::optional<std::string> region,
                      boost::optional<std::string> root,
                      boost::optional<std::string> storage_class_str)
    {
      boost::optional<int64_t> capacity;
      if (capacity_repr)
        capacity = convert_capacity(*capacity_repr);
      std::unique_ptr<infinit::storage::StorageConfig> config;
      int types = (dropbox ? 1 : 0)
        + (filesystem ? 1 : 0)
        + (gcs ? 1 : 0)
        + (google_drive ? 1 : 0)
        + (s3 ? 1 : 0);
      if (types > 1)
        elle::err<CLIError>("only one storage type may be specified");
      if (gcs)
      {
        auto self = this->cli().as_user();
        if (!root)
          root = elle::print("{}_blocks", name);
        auto account = this->cli().infinit().credentials_gcs(
          mandatory(account_name, "account"));
        config = elle::make_unique<infinit::storage::GCSConfig>(
          name,
          mandatory(bucket, "bucket"),
          *root,
          self.name,
          account->refresh_token,
          std::move(capacity),
          std::move(description));
      }
      else if (s3)
      {
        if (!root)
          root = elle::sprintf("%s_blocks", name);
        auto account = this->cli().infinit().credentials_aws(
          mandatory(account_name, "account"));
        auto aws_credentials = aws::Credentials(
          account->access_key_id,
          account->secret_access_key,
          mandatory(region, "region"),
          mandatory(bucket, "bucket"),
          root.get(),
          endpoint);
        aws::S3::StorageClass storage_class = aws::S3::StorageClass::Default;
        if (storage_class_str)
        {
          auto sc = boost::algorithm::to_lower_copy(*storage_class_str);
          if (sc == "standard")
            storage_class = aws::S3::StorageClass::Standard;
          else if (sc == "standard_ia")
            storage_class = aws::S3::StorageClass::StandardIA;
          else if (sc == "reduced_redundancy")
            storage_class = aws::S3::StorageClass::ReducedRedundancy;
          else
            elle::err<CLIError>("unrecognized storage class: %s",
                                storage_class_str);
        }
        config = elle::make_unique<infinit::storage::S3StorageConfig>(
          name,
          std::move(aws_credentials),
          storage_class,
          std::move(capacity),
          std::move(description));
      }
      else if (dropbox)
      {
        if (!root)
          root = elle::sprintf("storage_%s", name);
        auto account = this->cli().infinit().credentials_dropbox(
          mandatory(account_name, "account"));
        config = elle::make_unique<infinit::storage::DropboxStorageConfig>(
          name, account->token, std::move(root), std::move(capacity),
          std::move(description));
      }
      else if (google_drive)
      {
        auto self = this->cli().as_user();
        if (!root)
          root = elle::sprintf(".infinit_%s", name);
        auto account = this->cli().infinit().credentials_google(
          mandatory(account_name, "account"));
        config =
          elle::make_unique<infinit::storage::GoogleDriveStorageConfig>
          (name,
           std::move(root),
           account->refresh_token,
           self.name,
           std::move(capacity),
           std::move(description));
      }
    #ifndef INFINIT_WINDOWS
      else if (ssh)
        config = elle::make_unique<infinit::storage::SFTPStorageConfig>(
          name, mandatory(host, "host"),
          mandatory(root, "path"),
          capacity,
          std::move(description));
    #endif
      else // filesystem by default
      {
        auto path = root ?
          infinit::canonical_folder(root.get()) :
          (infinit::xdg_data_home() / "blocks" / name);
        if (boost::filesystem::exists(path))
        {
          if (!boost::filesystem::is_directory(path))
            elle::err("path is not directory: %s", path);
          if (!boost::filesystem::is_empty(path))
            std::cout << "WARNING: Path is not empty: " << path << '\n'
                      << "WARNING: You may encounter unexpected behavior.\n";
        }
        config = elle::make_unique<infinit::storage::FilesystemStorageConfig>(
          name, std::move(path.string()), std::move(capacity),
          std::move(description));
      }
      if (auto o = this->cli().get_output(output, false))
      {
        elle::serialization::json::SerializerOut s(*o, false);
        s.serialize_forward(config);
      }
      else
      {
        this->cli().infinit().storage_save(name, config);
        this->cli().report_action("created", "storage", name);
      }
    }

    void
    Silo::mode_export(std::string name,
                      boost::optional<std::string> output)
    {
      auto o = this->cli().get_output(output);
      auto storage = this->cli().infinit().storage_get(name);
      elle::serialization::json::serialize(storage, *o, false);
      this->cli().report_exported(std::cout, "storage", name);
    }

    void
    Silo::mode_import(boost::optional<std::string> input)
    {
      auto i = this->cli().get_input(input);
      {
        auto storage = elle::serialization::json::deserialize<
          std::unique_ptr<infinit::storage::StorageConfig>>(*i, false);
        if (storage->name.size() == 0)
          elle::err("storage name is empty");
        // FIXME: no need to pass the name
        this->cli().infinit().storage_save(storage->name, storage);
        this->cli().report_imported("storage", storage->name);
      }
    }


    void
    Silo::mode_list()
    {
      auto storages = this->cli().infinit().storages_get();
      if (this->cli().script())
      {
        auto l = elle::json::Array{};
        for (auto const& storage: storages)
        {
          auto o = elle::json::Object{
            {"name", static_cast<std::string>(storage->name)},
            {"capacity", (storage->capacity ? pretty_print(storage->capacity.get())
                          : "unlimited")},
          };
          if (storage->description)
            o["description"] = storage->description.get();
          l.emplace_back(std::move(o));
        }
        elle::json::write(std::cout, l);
      }
      else
        for (auto const& storage: storages)
          elle::print(
            std::cout, "{0}{1? \"{1}\"}: {2}\n",
            storage->name, storage->description,
            storage->capacity ?
            pretty_print(storage->capacity.get()) :
            "unlimited");
    }

    void
    Silo::mode_delete(std::string name,
                      bool clear,
                      bool purge)
    {
      auto& infinit = this->cli().infinit();
      auto storage = infinit.storage_get(name);
      auto user = this->cli().as_user();
      auto fs_storage =
        dynamic_cast<infinit::storage::FilesystemStorageConfig*>(storage.get());
      if (clear && !fs_storage)
        elle::err("only filesystem storages can be cleared");
      if (purge)
        for (auto const& pair: infinit.storage_networks(name))
          for (auto const& user_name: pair.second)
            infinit.network_unlink(
              pair.first, infinit.user_get(user_name));
      if (clear)
      {
        try
        {
          boost::filesystem::remove_all(fs_storage->path);
          this->cli().report_action("cleared", "storage", fs_storage->name);
        }
        catch (boost::filesystem::filesystem_error const& e)
        {
          elle::err("unable to clear storage contents: %s", e.what());
        }
      }
      auto path = infinit._storage_path(name);
      if (boost::filesystem::remove(path))
        this->cli().report_action("deleted", "storage", storage->name);
      else
        elle::err("storage could not be deleted: %s", path);
    }
  }
}
