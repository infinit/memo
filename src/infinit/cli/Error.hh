#pragma once

#include <elle/das/cli.hh>

namespace infinit
{
  namespace cli
  {
    class CLIError
      : public elle::das::cli::Error
    {
    public:
      using Super = elle::das::cli::Error;
      using Super::Super;
      /// If specified, the current object.
      ///
      /// E.g., "network" for "infinit network foo".
      ELLE_ATTRIBUTE_RW(boost::optional<std::string>, object);
    };
  }
}
