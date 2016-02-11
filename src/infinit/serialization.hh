#ifndef INFINIT_SERIALIZATION_HH
# define INFINIT_SERIALIZATION_HH

# include <elle/Version.hh>
# include <elle/serialization/Serializer.hh>
# include <elle/unordered_map.hh>

namespace infinit
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

#endif
