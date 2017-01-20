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
    class Doctor
      : public Entity<Doctor>
    {
    public:
      Doctor(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::configuration));

      /*----------------------.
      | Mode: configuration.  |
      `----------------------*/
      using ModeConfiguration =
        Mode<decltype(binding(modes::mode_configuration,
                              cli::verbose = false,
                              cli::ignore_non_linked = false))>;
      ModeConfiguration configuration;
      void
      mode_configuration(bool verbose,
                         bool ignore_non_linked);
    };
  }
}
