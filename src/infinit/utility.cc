#include <infinit/utility.hh>
#include <infinit/version.hh>

namespace infinit
{
  elle::Version
  version()
  {
    return elle::Version(INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR);
  }
}
