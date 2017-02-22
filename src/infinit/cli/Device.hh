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
    class Device
      : public Object<Device>
    {
    public:
      Device(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::receive,
                                    cli::transmit));

      // Mode: receive.
      Mode<Device,
           decltype(modes::mode_receive),
           decltype(cli::user = false),
           decltype(cli::name = std::string{}),
           decltype(cli::passphrase = boost::none)>
      receive;
      void
      mode_receive(bool user,
                   std::string const& name,
                   boost::optional<std::string> const& passphrase);

      // Mode: transmit.
      Mode<Device,
           decltype(modes::mode_transmit),
           decltype(cli::user = false),
           decltype(cli::passphrase = boost::none),
           decltype(cli::no_countdown = false)>
      transmit;
      void
      mode_transmit(bool user,
                    boost::optional<std::string> const& passphrase,
                    bool no_countdown);
    };
  }
}
