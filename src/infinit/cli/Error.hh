#pragma once

#include <das/cli.hh>

namespace infinit
{
  namespace cli
  {
    class CLIError
      : public das::cli::Error
    {
      using Super = das::cli::Error;
      using Super::Super;
    };
  }
}
