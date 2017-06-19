#pragma once

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
    class Journal
      : public Object<Journal>
    {
    public:
      Journal(Memo& memo);
      using Modes
        = decltype(elle::meta::list(cli::describe,
                                    cli::export_,
                                    cli::stat));

      // describe.
      Mode<Journal,
           void (decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::operation = boost::optional<int>())),
           decltype(modes::mode_describe)>
      describe;
      void
      mode_describe(std::string const& network,
                    boost::optional<int> operation = boost::none);

      // export.
      Mode<Journal,
           void (decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::operation)::Formal<int>),
           decltype(modes::mode_export)>
      export_;
      void
      mode_export(std::string const& network,
                  int operation);

      // stat.
      Mode<Journal,
           void (decltype(cli::network = boost::optional<std::string>())),
           decltype(modes::mode_stat)>
      stat;
      void
      mode_stat(boost::optional<std::string> const& network);
    };
  }
}
