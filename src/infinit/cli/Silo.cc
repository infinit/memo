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
    using Error = das::cli::Error;

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
      , create(infinit)
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

    Silo::Create::Create(Infinit& infinit)
      : Object(infinit)
      , dropbox(
        "Store blocks on Dropbox",
        das::cli::Options{
          {"path", das::cli::Option{
              '\0', "directory where to store blocks", false}}},
        this->bind(modes::mode_dropbox,
                   cli::name,
                   cli::account,
                   cli::description = boost::none,
                   cli::capacity = boost::none,
                   cli::output = boost::none,
                   cli::path = boost::none))
      , filesystem(
        "Store blocks on local filesystem",
        das::cli::Options{
          {"path", das::cli::Option{
              '\0', "directory where to store blocks", false}}},
        this->bind(modes::mode_filesystem,
                   cli::name,
                   cli::description = boost::none,
                   cli::capacity = boost::none,
                   cli::output = boost::none,
                   cli::path = boost::none))
      , gcs(
        "Store blocks on Google Cloud Storage",
        das::cli::Options{
          {"path", das::cli::Option{
              '\0', "directory where to store blocks", false}}},
        this->bind(modes::mode_gcs,
                   cli::name,
                   cli::account,
                   cli::bucket,
                   cli::description = boost::none,
                   cli::capacity = boost::none,
                   cli::output = boost::none,
                   cli::path = boost::none))
      , google_drive(
        "Store blocks on Google Drive",
        das::cli::Options{
          {"path", das::cli::Option{
              '\0', "directory where to store blocks", false}}},
        this->bind(modes::mode_google_drive,
                   cli::name,
                   cli::account,
                   cli::description = boost::none,
                   cli::capacity = boost::none,
                   cli::output = boost::none,
                   cli::path = boost::none))
      , s3(
        "Store blocks on AWS S3",
        das::cli::Options{
          {"path", das::cli::Option{
              '\0', "directory where to store blocks", false}}},
        this->bind(modes::mode_s3,
                   cli::name,
                   cli::account,
                   cli::bucket,
                   cli::region,
                   cli::description = boost::none,
                   cli::capacity = boost::none,
                   cli::output = boost::none,
                   cli::endpoint = "amazonaws.com",
                   cli::storage_class = boost::none,
                   cli::path = boost::none))
    {}

    void
    Silo::Create::mode_dropbox(std::string const& name,
                               std::string const& account_name,
                               boost::optional<std::string> description,
                               boost::optional<std::string> capacity_repr,
                               boost::optional<std::string> output,
                               boost::optional<std::string> root)
    {
      boost::optional<int64_t> capacity;
      if (capacity_repr)
        capacity = convert_capacity(*capacity_repr);
      if (!root)
        root = elle::sprintf("storage_%s", name);
      auto account = this->cli().infinit().credentials_dropbox(account_name);
      std::unique_ptr<infinit::storage::StorageConfig> config =
        elle::make_unique<infinit::storage::DropboxStorageConfig>(
          name,
          account->token,
          std::move(root),
          std::move(capacity),
          std::move(description));
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
    Silo::Create::mode_filesystem(std::string const& name,
                                  boost::optional<std::string> description,
                                  boost::optional<std::string> capacity_repr,
                                  boost::optional<std::string> output,
                                  boost::optional<std::string> root)
    {
      boost::optional<int64_t> capacity;
      if (capacity_repr)
        capacity = convert_capacity(*capacity_repr);
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
      std::unique_ptr<infinit::storage::StorageConfig> config =
        elle::make_unique<infinit::storage::FilesystemStorageConfig>(
          name,
          std::move(path.string()),
          std::move(capacity),
          std::move(description));
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
    Silo::Create::mode_gcs(std::string const& name,
                           std::string const& account_name,
                           std::string const& bucket,
                           boost::optional<std::string> description,
                           boost::optional<std::string> capacity_repr,
                           boost::optional<std::string> output,
                           boost::optional<std::string> root)
    {
      auto self = this->cli().as_user();
      boost::optional<int64_t> capacity;
      if (capacity_repr)
        capacity = convert_capacity(*capacity_repr);
      if (!root)
        root = elle::print("{}_blocks", name);
      auto account = this->cli().infinit().credentials_gcs(account_name);
      std::unique_ptr<infinit::storage::StorageConfig> config =
        elle::make_unique<infinit::storage::GCSConfig>(
          name,
          bucket,
          root.get(),
          self.name,
          account->refresh_token,
          std::move(capacity),
          std::move(description));
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
    Silo::Create::mode_s3(std::string const& name,
                          std::string const& account_name,
                          std::string const& bucket,
                          std::string const& region,
                          boost::optional<std::string> description,
                          boost::optional<std::string> capacity_repr,
                          boost::optional<std::string> output,
                          std::string const& endpoint,
                          boost::optional<std::string> const& storage_class_str,
                          boost::optional<std::string> root)
    {
      boost::optional<int64_t> capacity;
      if (capacity_repr)
        capacity = convert_capacity(*capacity_repr);
      if (!root)
        root = elle::sprintf("%s_blocks", name);
      auto account = this->cli().infinit().credentials_aws(account_name);
      auto aws_credentials = aws::Credentials(
        account->access_key_id,
        account->secret_access_key,
        region,
        bucket,
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
          elle::err<Error>("unrecognized storage class: %s",
                           storage_class_str);
      }
      std::unique_ptr<infinit::storage::StorageConfig> config =
        elle::make_unique<infinit::storage::S3StorageConfig>(
          name,
          std::move(aws_credentials),
          storage_class,
          std::move(capacity),
          std::move(description));
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
    Silo::Create::mode_google_drive(std::string const& name,
                                    std::string const& account_name,
                                    boost::optional<std::string> description,
                                    boost::optional<std::string> capacity_repr,
                                    boost::optional<std::string> output,
                                    boost::optional<std::string> root)
    {
      auto self = this->cli().as_user();
      boost::optional<int64_t> capacity;
      if (capacity_repr)
        capacity = convert_capacity(*capacity_repr);
      if (!root)
        root = elle::sprintf(".infinit_%s", name);
      auto account = this->cli().infinit().credentials_google(account_name);
      std::unique_ptr<infinit::storage::StorageConfig> config =
        elle::make_unique<infinit::storage::GoogleDriveStorageConfig>(
          name,
          std::move(root),
          account->refresh_token,
          self.name,
          std::move(capacity),
          std::move(description));
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
    Silo::mode_export(std::string const& name,
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
    Silo::mode_delete(std::string const& name,
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

#include <infinit/cli/Object.hxx>

namespace infinit
{
  namespace cli
  {
    template
    class Object<Silo>;
    template
    class Object<Silo::Create, Silo>;
  }
}
