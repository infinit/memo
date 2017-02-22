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
    class Block
      : public Object<Block>
    {
    public:
      Block(Infinit& infinit);
      using Modes
        = decltype(elle::meta::list(cli::deserialize));

      using Paths = std::vector<std::string>;

      // Deserialize.
      Mode<Block,
           decltype(modes::mode_deserialize),
           decltype(cli::output = boost::none),
           decltype(cli::paths = Paths{})>
      deserialize;
      void
      mode_deserialize(boost::optional<std::string> output,
                       Paths const& paths);
    };
  }
}
