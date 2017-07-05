#pragma once

#include <elle/das/cli.hh>

#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

namespace memo
{
  namespace cli
  {
    class Block
      : public Object<Block>
    {
    public:
      Block(Memo& memo);
      using Modes
        = decltype(elle::meta::list(cli::deserialize));

      using Paths = std::vector<std::string>;

      // Deserialize.
      Mode<Block,
           void (decltype(cli::output = boost::optional<std::string>()),
                 decltype(cli::paths = Paths{})),
           decltype(modes::mode_deserialize)>
      deserialize;
      void
      mode_deserialize(boost::optional<std::string> output,
                       Paths const& paths);
    };
  }
}
