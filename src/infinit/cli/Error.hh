#pragma once

#include <elle/das/cli.hh>

namespace infinit
{
  namespace cli
  {
    class CLIError
      : public elle::das::cli::Error
    {
      using Super = elle::das::cli::Error;
      using Super::Super;
    };
  }
}
