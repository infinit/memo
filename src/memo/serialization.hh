#pragma once

#include <elle/Version.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/unordered_map.hh>

namespace memo
{
  struct serialization_tag
  {
    using Dependencies
      = elle::unordered_map<elle::Version,
                            elle::serialization::Serializer::Versions>;
    static elle::Version const version;
    static Dependencies const dependencies;
  };

  elle::Version
  elle_serialization_version(elle::Version const& memo_version);
}
