#include <infinit/cli/Daemon.hh>

#include <infinit/cli/Infinit.hh>

ELLE_LOG_COMPONENT("cli.daemon");

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Daemon::Daemon(Infinit& infinit)
      : Entity(infinit)
    {}
  }
}
