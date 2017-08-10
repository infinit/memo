#include <memo/cli/Silo.hh>

#include <iostream>

#include <boost/algorithm/string/case_conv.hpp>

#include <elle/bytes.hh>
#include <elle/print.hh>

#include <memo/silo/Dropbox.hh>
#include <memo/silo/Filesystem.hh>
#include <memo/silo/GCS.hh>
#include <memo/silo/GoogleDrive.hh>
#include <memo/silo/S3.hh>
#ifndef ELLE_WINDOWS
# include <memo/silo/sftp.hh>
#endif
#include <memo/cli/Memo.hh>
#include <memo/cli/utility.hh>

ELLE_LOG_COMPONENT("cli.block");

namespace memo
{
  namespace cli
  {
    namespace bfs = boost::filesystem;

    Silo::Silo(Memo& memo)
      : Object(memo)
      , create(memo)
      , delete_(*this,
                "Delete local silo",
                name,
                clear_content = false,
                purge = false)
      , export_(*this,
                "Export local silo",
                name,
                output = boost::none)
      , import(*this,
               "Import local silo",
               input = boost::none)
      , list(*this, "List local silos")
    {}

    Silo::Create::Create(Memo& memo)
      : Object(memo)
      MEMO_ENTREPRISE(
      , dropbox(*this,
                "Store blocks on Dropbox",
                elle::das::cli::Options{
                  {"path", elle::das::cli::Option{
                      '\0', "directory where to store blocks", false}}},
                cli::name,
                cli::account,
                cli::description = boost::none,
                cli::capacity = boost::none,
                cli::output = boost::none,
                cli::path = boost::none)
      )
      , filesystem(*this,
                   "Store blocks on local filesystem",
                   elle::das::cli::Options{
                     {"path", elle::das::cli::Option{
                         '\0', "directory where to store blocks", false}}},
                   cli::name,
                   cli::description = boost::none,
                   cli::capacity = boost::none,
                   cli::output = boost::none,
                   cli::path = boost::none)
      MEMO_ENTREPRISE(
      , gcs(*this,
            "Store blocks on Google Cloud Storage",
            elle::das::cli::Options{
              {"path", elle::das::cli::Option{
                  '\0', "directory where to store blocks", false}}},
            cli::name,
            cli::account,
            cli::bucket,
            cli::description = boost::none,
            cli::capacity = boost::none,
            cli::output = boost::none,
            cli::path = boost::none)
      , google_drive(*this,
                     "Store blocks on Google Drive",
                     elle::das::cli::Options{
                       {"path", elle::das::cli::Option{
                           '\0', "directory where to store blocks", false}}},
                     cli::name,
                     cli::account,
                     cli::description = boost::none,
                     cli::capacity = boost::none,
                     cli::output = boost::none,
                     cli::path = boost::none)
      , s3(*this,
           "Store blocks on AWS S3",
           elle::das::cli::Options{
             {"path", elle::das::cli::Option{
                 '\0', "directory where to store blocks", false}}},
           cli::name,
           cli::account,
           cli::bucket,
           cli::region,
           cli::description = boost::none,
           cli::capacity = boost::none,
           cli::output = boost::none,
           cli::endpoint = "amazonaws.com",
           cli::silo_class = boost::none,
           cli::path = boost::none)
      )
    {}

    namespace
    {
      void
      mode_create(Memo& cli,
                  boost::optional<std::string> output,
                  std::unique_ptr<memo::silo::SiloConfig> config)
      {
        if (auto o = cli.get_output(output, false))
        {
          elle::serialization::json::SerializerOut s(*o, false);
          s.serialize_forward(config);
        }
        else
          cli.backend().silo_save(config->name, config);
      }
    }

    MEMO_ENTREPRISE(
    void
    Silo::Create::mode_dropbox(std::string const& name,
                               std::string const& account_name,
                               boost::optional<std::string> description,
                               boost::optional<std::string> capacity,
                               boost::optional<std::string> output,
                               boost::optional<std::string> root)
    {
      if (!root)
        root = elle::sprintf("storage_%s", name);
      auto account = this->cli().backend().credentials_dropbox(account_name);
      mode_create(
        this->cli(),
        output,
        std::make_unique<memo::silo::DropboxSiloConfig>(
          name,
          account->token,
          std::move(root),
          elle::convert_capacity(capacity),
          std::move(description)));
    })

    void
    Silo::Create::mode_filesystem(std::string const& name,
                                  boost::optional<std::string> description,
                                  boost::optional<std::string> capacity,
                                  boost::optional<std::string> output,
                                  boost::optional<std::string> root)
    {
      auto const path = root ? memo::canonical_folder(root.get())
        : (memo::xdg_data_home() / "blocks" / name);
      if (bfs::exists(path))
      {
        if (!bfs::is_directory(path))
          elle::err("path is not directory: %s", path);
        if (!bfs::is_empty(path))
          std::cout << "WARNING: Path is not empty: " << path << '\n'
                    << "WARNING: You may encounter unexpected behavior.\n";
      }
      mode_create(
        this->cli(),
        output,
        std::make_unique<memo::silo::FilesystemSiloConfig>(
          name,
          std::move(path.string()),
          elle::convert_capacity(capacity),
          std::move(description)));
    }

    MEMO_ENTREPRISE(
    void
    Silo::Create::mode_gcs(std::string const& name,
                           std::string const& account_name,
                           std::string const& bucket,
                           boost::optional<std::string> description,
                           boost::optional<std::string> capacity,
                           boost::optional<std::string> output,
                           boost::optional<std::string> root)
    {
      auto self = this->cli().as_user();
      auto const account = this->cli().backend().credentials_gcs(account_name);
      mode_create(
        this->cli(),
        output,
        std::make_unique<memo::silo::GCSConfig>(
          name,
          bucket,
          root.value_or(elle::print("{}_blocks", name)),
          self.name,
          account->refresh_token,
          elle::convert_capacity(capacity),
          std::move(description)));
    }

    void
    Silo::Create::mode_s3(std::string const& name,
                          std::string const& account_name,
                          std::string const& bucket,
                          std::string const& region,
                          boost::optional<std::string> description,
                          boost::optional<std::string> capacity,
                          boost::optional<std::string> output,
                          std::string const& endpoint,
                          boost::optional<std::string> const& silo_class_str,
                          boost::optional<std::string> root)
    {
      if (!root)
        root = elle::sprintf("%s_blocks", name);
      auto account = this->cli().backend().credentials_aws(account_name);
      auto aws_credentials = elle::service::aws::Credentials(
        account->access_key_id,
        account->secret_access_key,
        region,
        bucket,
        root.get(),
        endpoint);
      elle::service::aws::S3::StorageClass silo_class = elle::service::aws::S3::StorageClass::Default;
      if (silo_class_str)
      {
        auto sc = boost::algorithm::to_lower_copy(*silo_class_str);
        if (sc == "standard")
          silo_class = elle::service::aws::S3::StorageClass::Standard;
        else if (sc == "standard_ia")
          silo_class = elle::service::aws::S3::StorageClass::StandardIA;
        else if (sc == "reduced_redundancy")
          silo_class = elle::service::aws::S3::StorageClass::ReducedRedundancy;
        else
          elle::err<CLIError>("unrecognized storage class: %s",
                              silo_class_str);
      }
      mode_create(
        this->cli(),
        output,
        std::make_unique<memo::silo::S3SiloConfig>(
          name,
          std::move(aws_credentials),
          silo_class,
          elle::convert_capacity(capacity),
          std::move(description)));
    }

    void
    Silo::Create::mode_google_drive(std::string const& name,
                                    std::string const& account_name,
                                    boost::optional<std::string> description,
                                    boost::optional<std::string> capacity,
                                    boost::optional<std::string> output,
                                    boost::optional<std::string> root)
    {
      auto self = this->cli().as_user();
      if (!root)
        root = elle::sprintf(".infinit_%s", name);
      auto account = this->cli().backend().credentials_google(account_name);
      mode_create(
        this->cli(),
        output,
        std::make_unique<memo::silo::GoogleDriveSiloConfig>(
          name,
          std::move(root),
          account->refresh_token,
          self.name,
          elle::convert_capacity(capacity),
          std::move(description)));
    })

    void
    Silo::mode_export(std::string const& name,
                      boost::optional<std::string> output)
    {
      auto o = this->cli().get_output(output);
      auto silo = this->cli().backend().silo_get(name);
      elle::serialization::json::serialize(silo, *o, false);
      this->cli().report_exported(std::cout, "silo", name);
    }

    void
    Silo::mode_import(boost::optional<std::string> input)
    {
      auto i = this->cli().get_input(input);
      {
        auto silo = elle::serialization::json::deserialize<
          std::unique_ptr<memo::silo::SiloConfig>>(*i, false);
        if (silo->name.empty())
          elle::err("silo name is empty");
        // FIXME: no need to pass the name
        this->cli().backend().silo_save(silo->name, silo);
        this->cli().report_imported("silo", silo->name);
      }
    }


    void
    Silo::mode_list()
    {
      auto const capacity = [](auto const& silo)
        {
          return (silo->capacity
                  ? elle::human_data_size(*silo->capacity)
                  : "unlimited");
        };
      auto silos = this->cli().backend().silos_get();
      if (this->cli().script())
      {
        auto l = elle::json::Array{};
        for (auto const& silo: silos)
        {
          auto o = elle::json::Object{
            {"name", static_cast<std::string>(silo->name)},
            {"capacity", capacity(silo)},
          };
          if (silo->description)
            o["description"] = *silo->description;
          l.emplace_back(std::move(o));
        }
        elle::json::write(std::cout, l);
      }
      else
        for (auto const& silo: silos)
          elle::print(
            std::cout, "{0}{1? \"{1}\"}: {2}\n",
            silo->name, silo->description,
            capacity(silo));
    }

    void
    Silo::mode_delete(std::string const& name,
                      bool clear,
                      bool purge)
    {
      auto& memo = this->cli().backend();
      auto silo = memo.silo_get(name);
      auto fs_silo =
        dynamic_cast<memo::silo::FilesystemSiloConfig*>(silo.get());
      if (clear && !fs_silo)
        elle::err("only filesystem silos can be cleared");
      if (purge)
        for (auto const& pair: memo.silo_networks(name))
          for (auto const& user_name: pair.second)
            memo.network_unlink(
              pair.first, memo.user_get(user_name));
      memo.silo_delete(silo, clear);
    }
  }
}
