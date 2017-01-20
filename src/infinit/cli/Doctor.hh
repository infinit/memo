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
        = decltype(elle::meta::list(cli::configuration,
                                    cli::connectivity));

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

      /*---------------------.
      | Mode: connectivity.  |
      `---------------------*/
      using ModeConnectivity =
        Mode<decltype(binding(modes::mode_connectivity,
                              cli::upnp_tcp_port = boost::none,
                              cli::upnp_udt_port = boost::none,
                              cli::server = std::string{"connectivity.infinit.sh"},
                              cli::verbose = false))>;
      ModeConnectivity connectivity;
      void
      mode_connectivity(boost::optional<uint16_t> upnp_tcp_port,
                        boost::optional<uint16_t> upnp_udt_port,
                        boost::optional<std::string> const& server,
                        bool verbose);
    };
  }
}
