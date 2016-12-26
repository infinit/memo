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
      Mode<decltype(binding(modes::mode_add,
                            name = boost::none,
                            aws = false,
                            dropbox = false,
                            gcs = false,
                            google_drive = false))>
      add;
      void
      mode_add(boost::optional<std::string> const& account,
               bool aws, bool dropbox, bool gcs, bool google_drive);

      // Delete.
      Mode<decltype(binding(modes::mode_delete,
                            name,
                            aws = false,
                            dropbox = false,
                            gcs = false,
                            google_drive = false))>
      delete_;
      void
      mode_delete(std::string const& account,
                  bool aws, bool dropbox, bool gcs, bool google_drive);

      // Fetch.
      Mode<decltype(binding(modes::mode_fetch,
                            name = boost::none,
                            aws = false,
                            dropbox = false,
                            gcs = false,
                            google_drive = false))>
      fetch;
      void
      mode_fetch(boost::optional<std::string> const& account,
                 bool aws, bool dropbox, bool gcs, bool google_drive);

      // Pull.
      Mode<decltype(binding(modes::mode_pull,
                            account = boost::none,
                            aws = false,
                            dropbox = false,
                            gcs = false,
                            google_drive = false))>
      pull;
      void
      mode_pull(boost::optional<std::string> const& account,
                bool aws, bool dropbox, bool gcs, bool google_drive);

      // List.
      Mode<decltype(binding(modes::mode_list,
                            aws = false,
                            dropbox = false,
                            gcs = false,
                            google_drive = false))>
      list;
      void
      mode_list(bool aws, bool dropbox, bool gcs, bool google_drive);
    };
  }
}
