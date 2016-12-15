#pragma once

#include <string>

#include <elle/unordered_map.hh>

namespace infinit
{
  namespace cli
  {
    struct VarMap
    {
      /// Variable name -> value.
      using Map = elle::unordered_map<std::string, std::string>;

      template <typename... Args>
      VarMap(Args&&... args)
        : vars(std::forward<Args>(args)...)
      {}

      VarMap(std::initializer_list<Map::value_type> l)
        : vars(std::move(l))
      {}

      /// Perform metavariable substitution.
      std::string
      expand(std::string const& s) const;

      Map vars;
    };
  }
}
