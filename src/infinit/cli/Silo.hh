#pragma once

#include <boost/optional.hpp>

#include <das/cli.hh>

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
        using Modes = decltype(elle::meta::list(cli::filesystem,
                                                cli::dropbox,
                                                cli::gcs,
                                                cli::google_drive,
                                                cli::s3));
        Create(Infinit& infinit);
        Mode<decltype(binding(modes::mode_dropbox,
                              cli::name,
                              cli::account,
                              cli::description = boost::none,
                              cli::capacity = boost::none,
                              cli::output = boost::none,
                              cli::path = boost::none))>
        dropbox;
        void
        mode_dropbox(std::string const& name,
                     std::string const& account,
                     boost::optional<std::string> description = {},
                     boost::optional<std::string> capacity = {},
                     boost::optional<std::string> output = {},
                     boost::optional<std::string> root = {});
        Mode<decltype(binding(modes::mode_filesystem,
                              cli::name,
                              cli::description = boost::none,
                              cli::capacity = boost::none,
                              cli::output = boost::none,
                              cli::path = boost::none))>
        filesystem;
        void
        mode_filesystem(std::string const& name,
                        boost::optional<std::string> description,
                        boost::optional<std::string> capacity,
                        boost::optional<std::string> output,
                        boost::optional<std::string> path);
        Mode<decltype(binding(modes::mode_gcs,
                              cli::name,
                              cli::account,
                              cli::bucket,
                              cli::description = boost::none,
                              cli::capacity = boost::none,
                              cli::output = boost::none,
                              cli::path = boost::none))>
        gcs;
        void
        mode_gcs(std::string const& name,
                 std::string const& account,
                 std::string const& bucket,
                 boost::optional<std::string> description = {},
                 boost::optional<std::string> capacity = {},
                 boost::optional<std::string> output = {},
                 boost::optional<std::string> path = {});
        Mode<decltype(binding(modes::mode_google_drive,
                              cli::name,
                              cli::account,
                              cli::description = boost::none,
                              cli::capacity = boost::none,
                              cli::output = boost::none,
                              cli::path = boost::none))>
        google_drive;
        void
        mode_google_drive(std::string const& name,
                     std::string const& account,
                     boost::optional<std::string> description = {},
                     boost::optional<std::string> capacity = {},
                     boost::optional<std::string> output = {},
                     boost::optional<std::string> root = {});
        Mode<decltype(binding(modes::mode_s3,
                              cli::name,
                              cli::account,
                              cli::bucket,
                              cli::region,
                              cli::description = boost::none,
                              cli::capacity = boost::none,
                              cli::output = boost::none,
                              cli::endpoint = "amazonaws.com",
                              cli::storage_class = boost::none,
                              cli::path = boost::none))>
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
        std::string const description = "Create local silo";
      } create;

      // Delete
      Mode<decltype(binding(modes::mode_delete,
                            name,
                            clear_content = false,
                            purge = false))>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool clear,
                  bool purge);

      // Export
      Mode<decltype(binding(modes::mode_export,
                            name,
                            output = boost::none))>
      export_;
      void
      mode_export(std::string const& name,
                  boost::optional<std::string> output);

      // Import
      Mode<decltype(binding(modes::mode_import,
                            input = boost::none))>
      import;
      void
      mode_import(boost::optional<std::string> input);

      // List
      Mode<decltype(binding(modes::mode_list))>
      list;
      void
      mode_list();
    };
  }
}
