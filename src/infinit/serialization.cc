#include <infinit/serialization.hh>
#include <infinit/version.hh>

namespace infinit
{
  elle::Version serialization_tag::version(
    INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR);
}
