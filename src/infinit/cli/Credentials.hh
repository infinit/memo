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
    class Credentials
      : public Object<Credentials>
    {
    public:
      Credentials(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::add,
                                    cli::delete_,
                                    cli::fetch,
                                    cli::pull,
                                    cli::list));

      // Add.
      Mode<Credentials,
           decltype(modes::mode_add),
           decltype(name),
           decltype(aws = false),
           decltype(dropbox = false),
           decltype(gcs = false),
           decltype(google_drive = false)>
      add;
      void
      mode_add(std::string const& account,
               bool aws,
               bool dropbox,
               bool gcs,
               bool google_drive);

      // Delete.
      Mode<Credentials,
           decltype(modes::mode_delete),
           decltype(name),
           decltype(aws = false),
           decltype(dropbox = false),
           decltype(gcs = false),
           decltype(google_drive = false),
           decltype(cli::pull = false)>
      delete_;
      void
      mode_delete(std::string const& account,
                  bool aws, bool dropbox, bool gcs, bool google_drive,
                  bool pull);

      // Fetch.
      Mode<Credentials,
           decltype(modes::mode_fetch),
           decltype(name = boost::none),
           decltype(aws = false),
           decltype(dropbox = false),
           decltype(gcs = false),
           decltype(google_drive = false)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> const& account,
                 bool aws, bool dropbox, bool gcs, bool google_drive);

      // Pull.
      Mode<Credentials,
           decltype(modes::mode_pull),
           decltype(account = boost::none),
           decltype(aws = false),
           decltype(dropbox = false),
           decltype(gcs = false),
           decltype(google_drive = false)>
      pull;
      void
      mode_pull(boost::optional<std::string> const& account,
                bool aws, bool dropbox, bool gcs, bool google_drive);

      // List.
      Mode<Credentials,
           decltype(modes::mode_list),
           decltype(aws = false),
           decltype(dropbox = false),
           decltype(gcs = false),
           decltype(google_drive = false)>
      list;
      void
      mode_list(bool aws, bool dropbox, bool gcs, bool google_drive);
    };
  }
}
