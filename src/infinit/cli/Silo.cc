#include <infinit/cli/Silo.hh>

#include <iostream>

#include <infinit/cli/Infinit.hh>

namespace infinit
{
  namespace cli
  {
    static das::cli::Options options = {
      {"help", {'h', "show this help message"}},
    };

    Silo::Silo(Infinit& infinit)
      : Entity(infinit)
      , list("List local silos",
             das::cli::Options(),
             this->bind(modes::mode_list))
    {}

    void
    Silo::mode_list()
    {}
  }
}
