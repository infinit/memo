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
    class Device
      : public Entity<Device>
    {
    public:
      Device(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::receive,
                                    cli::transmit));

      // Mode: receive.
      Mode<decltype(binding(modes::mode_receive,
                            cli::user = false,
                            cli::name = std::string{},
                            cli::passphrase = boost::none))>
      receive;
      void
      mode_receive(bool user,
                   boost::optional<std::string> const& name,
                   boost::optional<std::string> const& passphrase);

      // Mode: transmit.
      Mode<decltype(binding(modes::mode_transmit,
                            cli::user = false,
                            cli::name = std::string{},
                            cli::passphrase = boost::none,
                            cli::no_countdown = false))>
      transmit;
      void
      mode_transmit(bool user,
                    boost::optional<std::string> const& name,
                    boost::optional<std::string> const& passphrase,
                    bool no_countdown);
    };
  }
}
