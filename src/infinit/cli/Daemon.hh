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
    class Daemon
      : public Entity<Daemon>
    {
    public:
      Daemon(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::status,
                                    cli::stop));

      /*---------------.
      | Mode: status.  |
      `---------------*/
      using ModeStatus =
        Mode<decltype(binding(modes::mode_status))>;
      ModeStatus status;
      void
      mode_status();

      /*-------------.
      | Mode: stop.  |
      `-------------*/
      using ModeStop =
        Mode<decltype(binding(modes::mode_stop))>;
      ModeStop stop;
      void
      mode_stop();
    };
  }
}
