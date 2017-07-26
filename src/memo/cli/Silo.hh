#pragma once

#include <boost/optional.hpp>

#include <elle/das/cli.hh>

#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

namespace memo
{
  namespace cli
  {
    class Silo
      : public Object<Silo>
    {
    public:
      Silo(Memo& memo);
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
                                 MEMO_ENTREPRISE(,cli::dropbox)
                                 MEMO_ENTREPRISE(,cli::gcs)
                                 MEMO_ENTREPRISE(,cli::google_drive)
                                 MEMO_ENTREPRISE(,cli::s3)));
        Create(Memo& memo);

        MEMO_ENTREPRISE(

        /// Dropbox.
        Mode<Create,
             void (decltype(cli::name)::Formal<std::string const&>,
                   decltype(cli::account)::Formal<std::string const&>,
                   decltype(cli::description = boost::optional<std::string>()),
                   decltype(cli::capacity = boost::optional<std::string>()),
                   decltype(cli::output = boost::optional<std::string>()),
                   decltype(cli::path = boost::optional<std::string>())),
             decltype(modes::mode_dropbox)>
        dropbox;
        void
        mode_dropbox(std::string const& name,
                     std::string const& account,
                     boost::optional<std::string> description = {},
                     boost::optional<std::string> capacity = {},
                     boost::optional<std::string> output = {},
                     boost::optional<std::string> root = {});

        );

        /// Filesystem.
        Mode<Create,
             void (decltype(cli::name)::Formal<std::string const&>,
                   decltype(cli::description = boost::optional<std::string>()),
                   decltype(cli::capacity = boost::optional<std::string>()),
                   decltype(cli::output = boost::optional<std::string>()),
                   decltype(cli::path = boost::optional<std::string>())),
             decltype(modes::mode_filesystem)>
        filesystem;
        void
        mode_filesystem(std::string const& name,
                        boost::optional<std::string> description,
                        boost::optional<std::string> capacity,
                        boost::optional<std::string> output,
                        boost::optional<std::string> path);

        MEMO_ENTREPRISE(

        /// GCS.
        Mode<Create,
             void (decltype(cli::name)::Formal<std::string const&>,
                   decltype(cli::account)::Formal<std::string const&>,
                   decltype(cli::bucket)::Formal<std::string const&>,
                   decltype(cli::description = boost::optional<std::string>()),
                   decltype(cli::capacity = boost::optional<std::string>()),
                   decltype(cli::output = boost::optional<std::string>()),
                   decltype(cli::path = boost::optional<std::string>())),
             decltype(modes::mode_gcs)>
        gcs;
        void
        mode_gcs(std::string const& name,
                 std::string const& account,
                 std::string const& bucket,
                 boost::optional<std::string> description = {},
                 boost::optional<std::string> capacity = {},
                 boost::optional<std::string> output = {},
                 boost::optional<std::string> path = {});

        /// Google Drive.
        Mode<Create,
             void (decltype(cli::name)::Formal<std::string const&>,
                   decltype(cli::account)::Formal<std::string const&>,
                   decltype(cli::description = boost::optional<std::string>()),
                   decltype(cli::capacity = boost::optional<std::string>()),
                   decltype(cli::output = boost::optional<std::string>()),
                   decltype(cli::path = boost::optional<std::string>())),
             decltype(modes::mode_google_drive)>
        google_drive;
        void
        mode_google_drive(std::string const& name,
                          std::string const& account,
                          boost::optional<std::string> description = {},
                          boost::optional<std::string> capacity = {},
                          boost::optional<std::string> output = {},
                          boost::optional<std::string> root = {});

        /// S3.
        Mode<Create,
             void (decltype(cli::name)::Formal<std::string const&>,
                   decltype(cli::account)::Formal<std::string const&>,
                   decltype(cli::bucket)::Formal<std::string const&>,
                   decltype(cli::region)::Formal<std::string const&>,
                   decltype(cli::description = boost::optional<std::string>()),
                   decltype(cli::capacity = boost::optional<std::string>()),
                   decltype(cli::output = boost::optional<std::string>()),
                   decltype(cli::endpoint = std::string()),
                   decltype(cli::silo_class = boost::optional<std::string>()),
                   decltype(cli::path = boost::optional<std::string>())),
             decltype(modes::mode_s3)>
        s3;
        void
        mode_s3(std::string const& name,
                std::string const& account,
                std::string const& bucket,
                std::string const& region,
                boost::optional<std::string> description = {},
                boost::optional<std::string> capacity = {},
                boost::optional<std::string> output = {},
                std::string const& endpoint = std::string(),
                boost::optional<std::string> const& silo_class = {},
                boost::optional<std::string> path = {});

        );

        std::string const description = "Create local silo";
      } create;

      // Delete
      Mode<Silo,
           void (decltype(name)::Formal<std::string const&>,
                 decltype(clear_content = false),
                 decltype(purge = false)),
           decltype(modes::mode_delete)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool clear,
                  bool purge);

      // Export
      Mode<Silo,
           void (decltype(name)::Formal<std::string const&>,
                 decltype(output = boost::optional<std::string>())),
           decltype(modes::mode_export)>
      export_;
      void
      mode_export(std::string const& name,
                  boost::optional<std::string> output);

      // Import
      Mode<Silo,
           void (decltype(input = boost::optional<std::string>())),
           decltype(modes::mode_import)>
      import;
      void
      mode_import(boost::optional<std::string> input);

      // List
      Mode<Silo,
           void (),
           decltype(modes::mode_list)>
      list;
      void
      mode_list();
    };
  }
}
