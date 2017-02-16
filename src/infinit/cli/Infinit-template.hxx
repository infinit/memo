#pragma once

namespace infinit
{
  namespace cli
  {
    template <typename Symbol>
    struct object
    {
      using type = bool;
      static
      bool
      value(infinit::cli::Infinit& infinit,
            std::vector<std::string>& args,
            bool& found);
    };
  }
}
