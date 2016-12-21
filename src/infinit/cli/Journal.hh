#pragma once

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
    class Journal
      : public Entity<Journal>
    {
    public:
      Journal(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::describe,
                                    cli::export_,
                                    cli::stat));

      // describe.
      Mode<decltype(binding(modes::mode_describe,
                            cli::network,
                            cli::operation = boost::none))>
      describe;
      void
      mode_describe(std::string const& network,
                    boost::optional<int> operation = boost::none);

      // export.
      Mode<decltype(binding(modes::mode_export,
                            cli::network,
                            cli::operation))>
      export_;
      void
      mode_export(std::string const& network,
                  int operation);

      // stat.
      Mode<decltype(binding(modes::mode_stat,
                            cli::network = boost::none))>
      stat;
      void
      mode_stat(boost::optional<std::string> const& network);
    };
  }
}
