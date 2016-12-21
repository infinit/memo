#include <infinit/cli/Journal.hh>

#include <infinit/cli/Infinit.hh>

ELLE_LOG_COMPONENT("cli.journal");

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Journal::Journal(Infinit& infinit)
      : Entity(infinit)
      , describe(
        "Describe asynchronous operation(s)",
        das::cli::Options(),
        this->bind(modes::mode_describe,
                   cli::network,
                   cli::operation = boost::none))
      , export_(
        "Export an operation",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   cli::network,
                   cli::operation))
      , stat(
        "Show the remaining asynchronous operations count and size",
        das::cli::Options(),
        this->bind(modes::mode_stat,
                   cli::network = boost::none))
    {}

    /*-----------------.
    | Mode: describe.  |
    `-----------------*/

    void
    Journal::mode_describe(std::string const& network,
                           boost::optional<int> operation)
    {}

    /*---------------.
    | Mode: export.  |
    `---------------*/

    void
    Journal::mode_export(std::string const& network,
                         int operation)
    {}

    /*--------------.
    | Mode: stats.  |
    `--------------*/

    void
    Journal::mode_stat(boost::optional<std::string> const& network)
    {}
  }
}
