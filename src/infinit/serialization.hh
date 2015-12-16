#ifndef INFINIT_SERIALIZATION_HH
# define INFINIT_SERIALIZATION_HH

# include <elle/Version.hh>
# include <elle/serialization/Serializer.hh>

namespace infinit
{
  struct serialization_tag
  {
    static elle::Version version;
    static std::unordered_map<
      elle::Version, elle::serialization::Serializer::Versions> dependencies;
  };
}

#endif
