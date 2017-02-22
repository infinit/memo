#pragma once

#include <boost/optional.hpp>

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Silo
      : public Object<Silo>
    {
    public:
      Silo(Infinit& cli);
      using Modes = decltype(elle::meta::list(cli::create,
                                              cli::delete_,
                                              cli::export_,
                                              cli::import,
                                              cli::list));
      using Objects = decltype(elle::meta::list(cli::create));

      // Create
      class Create
        : public Object<Create, Silo>
      {
      public:
        using Super = Object<Create, Silo>;
        using Modes = decltype(elle::meta::list(
                                 cli::filesystem
                                 INFINIT_ENTREPRISE(,cli::dropbox)
                                 INFINIT_ENTREPRISE(,cli::gcs)
                                 INFINIT_ENTREPRISE(,cli::google_drive)
                                 INFINIT_ENTREPRISE(,cli::s3)));
        Create(Infinit& infinit);

        INFINIT_ENTREPRISE(

        // Dropbox.
        Mode<Create,
             decltype(modes::mode_dropbox),
             decltype(cli::name),
             decltype(cli::account),
             decltype(cli::description = boost::none),
             decltype(cli::capacity = boost::none),
             decltype(cli::output = boost::none),
             decltype(cli::path = boost::none)>
        dropbox;
        void
        mode_dropbox(std::string const& name,
                     std::string const& account,
                     boost::optional<std::string> description = {},
                     boost::optional<std::string> capacity = {},
                     boost::optional<std::string> output = {},
                     boost::optional<std::string> root = {});

        );

        // Filesystem.
        Mode<Create,
             decltype(modes::mode_filesystem),
             decltype(cli::name),
             decltype(cli::description = boost::none),
             decltype(cli::capacity = boost::none),
             decltype(cli::output = boost::none),
             decltype(cli::path = boost::none)>
        filesystem;
        void
        mode_filesystem(std::string const& name,
                        boost::optional<std::string> description,
                        boost::optional<std::string> capacity,
                        boost::optional<std::string> output,
                        boost::optional<std::string> path);

        INFINIT_ENTREPRISE(

        Mode<Create,
             decltype(modes::mode_gcs),
             decltype(cli::name),
             decltype(cli::account),
             decltype(cli::bucket),
             decltype(cli::description = boost::none),
             decltype(cli::capacity = boost::none),
             decltype(cli::output = boost::none),
             decltype(cli::path = boost::none)>
        gcs;
        void
        mode_gcs(std::string const& name,
                 std::string const& account,
                 std::string const& bucket,
                 boost::optional<std::string> description = {},
                 boost::optional<std::string> capacity = {},
                 boost::optional<std::string> output = {},
                 boost::optional<std::string> path = {});

        Mode<Create,
             decltype(modes::mode_google_drive),
             decltype(cli::name),
             decltype(cli::account),
             decltype(cli::description = boost::none),
             decltype(cli::capacity = boost::none),
             decltype(cli::output = boost::none),
             decltype(cli::path = boost::none)>
        google_drive;
        void
        mode_google_drive(std::string const& name,
                     std::string const& account,
                     boost::optional<std::string> description = {},
                     boost::optional<std::string> capacity = {},
                     boost::optional<std::string> output = {},
                     boost::optional<std::string> root = {});

        Mode<Create,
             decltype(modes::mode_s3),
             decltype(cli::name),
             decltype(cli::account),
             decltype(cli::bucket),
             decltype(cli::region),
             decltype(cli::description = boost::none),
             decltype(cli::capacity = boost::none),
             decltype(cli::output = boost::none),
             decltype(cli::endpoint = "amazonaws.com"),
             decltype(cli::storage_class = boost::none),
             decltype(cli::path = boost::none)>
        s3;
        void
        mode_s3(std::string const& name,
                std::string const& account,
                std::string const& bucket,
                std::string const& region,
                boost::optional<std::string> description = {},
                boost::optional<std::string> capacity = {},
                boost::optional<std::string> output = {},
                std::string const& endpoint = "amazonaws.com",
                boost::optional<std::string> const& storage_class = {},
                boost::optional<std::string> path = {});

        );

        std::string const description = "Create local silo";
      } create;

      // Delete
      Mode<Silo,
           decltype(modes::mode_delete),
           decltype(name),
           decltype(clear_content = false),
           decltype(purge = false)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool clear,
                  bool purge);

      // Export
      Mode<Silo,
           decltype(modes::mode_export),
           decltype(name),
           decltype(output = boost::none)>
      export_;
      void
      mode_export(std::string const& name,
                  boost::optional<std::string> output);

      // Import
      Mode<Silo,
           decltype(modes::mode_import),
           decltype(input = boost::none)>
      import;
      void
      mode_import(boost::optional<std::string> input);

      // List
      Mode<Silo,
           decltype(modes::mode_list)>
      list;
      void
      mode_list();
    };
  }
}
