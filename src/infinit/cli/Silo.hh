#pragma once

#include <boost/optional.hpp>

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
    class Silo
      : public Entity<Silo>
    {
    public:
      Silo(Infinit& cli);
      using Modes = decltype(elle::meta::list(cli::list));
      Mode<decltype(binding(modes::mode_list))>
      list;
      void
      mode_list();
    };
  }
}
