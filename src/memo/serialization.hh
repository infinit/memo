#pragma once

#include <elle/Version.hh>
#include <elle/serialization/Serializer.hh>
#include <elle/unordered_map.hh>

namespace memo
{
  struct serialization_tag
  {
    static elle::Version version;
    static elle::unordered_map<
      elle::Version, elle::serialization::Serializer::Versions> dependencies;
  };

  elle::Version
  elle_serialization_version(elle::Version const& infinit_version);
}
