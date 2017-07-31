#include <memo/utility.hh>
#include <memo/serialization.hh>

namespace memo
{
  elle::Version const serialization_tag::version = memo::version();

#define DEFINE(MemoVersion, SerializationVersion)                    \
      { elle::Version MemoVersion,                                   \
      {{ elle::type_info<elle::serialization_tag>(), elle::Version SerializationVersion }}}

  serialization_tag::Dependencies const serialization_tag::dependencies =
    {
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
      DEFINE((0, 9, 1), (0, 4, 0)),
      DEFINE((0, 9, 2), (0, 4, 0)),
    };

#undef DEFINE

  elle::Version
  elle_serialization_version(elle::Version const& memo_version)
  {
    auto const versions = elle::serialization::get_serialization_versions<
      memo::serialization_tag>(memo_version);
    return versions.at(elle::type_info<elle::serialization_tag>());
  }
}
