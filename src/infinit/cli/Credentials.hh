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
    class Credentials
      : public Object<Credentials>
    {
    public:
      Credentials(Memo& memo);
      using Modes
        = decltype(elle::meta::list(cli::add,
                                    cli::delete_,
                                    cli::fetch,
                                    cli::pull,
                                    cli::list));

      // Add.
      Mode<Credentials,
           void (decltype(name)::Formal<std::string const&>,
                 decltype(aws = false),
                 decltype(dropbox = false),
                 decltype(gcs = false),
                 decltype(google_drive = false)),
           decltype(modes::mode_add)>
      add;
      void
      mode_add(std::string const& account,
               bool aws,
               bool dropbox,
               bool gcs,
               bool google_drive);

      // Delete.
      Mode<Credentials,
           void (decltype(name)::Formal<std::string const&>,
                 decltype(aws = false),
                 decltype(dropbox = false),
                 decltype(gcs = false),
                 decltype(google_drive = false),
                 decltype(cli::pull = false)),
           decltype(modes::mode_delete)>
      delete_;
      void
      mode_delete(std::string const& account,
                  bool aws,
                  bool dropbox,
                  bool gcs,
                  bool google_drive,
                  bool pull);

      // Fetch.
      Mode<Credentials,
           void (decltype(name = boost::optional<std::string>()),
                 decltype(aws = false),
                 decltype(dropbox = false),
                 decltype(gcs = false),
                 decltype(google_drive = false)),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> const& account,
                 bool aws,
                 bool dropbox,
                 bool gcs,
                 bool google_drive);

      // Pull.
      Mode<Credentials,
           void (decltype(account = boost::optional<std::string>()),
                 decltype(aws = false),
                 decltype(dropbox = false),
                 decltype(gcs = false),
                 decltype(google_drive = false)),
           decltype(modes::mode_pull)>
      pull;
      void
      mode_pull(boost::optional<std::string> const& account,
                bool aws,
                bool dropbox,
                bool gcs,
                bool google_drive);

      // List.
      Mode<Credentials,
           void (decltype(aws = false),
                 decltype(dropbox = false),
                 decltype(gcs = false),
                 decltype(google_drive = false)),
           decltype(modes::mode_list)>
      list;
      void
      mode_list(bool aws, bool dropbox, bool gcs, bool google_drive);
    };
  }
}
