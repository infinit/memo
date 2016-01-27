#include <infinit/serialization.hh>
#include <infinit/version.hh>

namespace infinit
{
  elle::Version serialization_tag::version(
    INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR);
  elle::unordered_map<elle::Version, elle::serialization::Serializer::Versions>
  serialization_tag::dependencies{{
    { elle::Version(0, 3, 0),
      {{ elle::type_info<elle::serialization_tag>(), elle::Version(0, 0, 0) }}},
    { elle::Version(0, 4, 0),
      {{ elle::type_info<elle::serialization_tag>(), elle::Version(0, 0, 0) }}},
    { elle::Version(0, 5, 0),
      {{ elle::type_info<elle::serialization_tag>(), elle::Version(0, 1, 0) }}},
  }};
}
