#pragma once

#include <boost/optional.hpp>

#include <das/cli.hh>

#include <infinit/cli/Entity.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Credentials
      : public Entity<Credentials>
    {
    public:
      Credentials(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::add,
                                    cli::list));

      // Add.
      Mode<decltype(binding(modes::mode_add,
                            account = boost::none,
                            aws = false,
                            dropbox = false,
                            gcs = false,
                            google_drive = false))>
      add;
      void
      mode_add(boost::optional<std::string> account,
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
