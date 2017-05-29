#include <infinit/utility.hh>
#include <infinit/serialization.hh>

namespace infinit
{
  elle::Version serialization_tag::version(infinit::version());

#define DEFINE(InfinitVersion, SerializationVersion)                    \
      { elle::Version InfinitVersion,                                   \
      {{ elle::type_info<elle::serialization_tag>(), elle::Version SerializationVersion }}}

  elle::unordered_map<elle::Version, elle::serialization::Serializer::Versions>
  serialization_tag::dependencies{{
    DEFINE((0, 3, 0), (0, 0, 0)),
    DEFINE((0, 4, 0), (0, 0, 0)),
    DEFINE((0, 5, 0), (0, 1, 0)),
    DEFINE((0, 5, 1), (0, 1, 0)),
    DEFINE((0, 5, 2), (0, 1, 0)),
    DEFINE((0, 5, 3), (0, 1, 0)),
    DEFINE((0, 5, 4), (0, 1, 0)),
    DEFINE((0, 5, 5), (0, 1, 0)),
    DEFINE((0, 6, 0), (0, 2, 0)),
    DEFINE((0, 6, 1), (0, 2, 0)),
    DEFINE((0, 6, 2), (0, 2, 0)),
    DEFINE((0, 7, 0), (0, 2, 0)),
    DEFINE((0, 7, 1), (0, 2, 0)),
    DEFINE((0, 7, 2), (0, 2, 0)),
    DEFINE((0, 7, 3), (0, 2, 0)),
    DEFINE((0, 8, 0), (0, 3, 0)),
    DEFINE((0, 9, 0), (0, 4, 0)),
  }};

#undef DEFINE

  elle::Version
  elle_serialization_version(elle::Version const& infinit_version)
  {
    auto versions = elle::serialization::get_serialization_versions<
      infinit::serialization_tag>(infinit_version);
    return versions.at(elle::type_info<elle::serialization_tag>());
  }
}
