#include <infinit/cli/Volume.hh>
#include <infinit/cli/Object.hxx>

namespace infinit
{
  namespace cli
  {
    template class Object<Volume>;
    template class Object<Volume::Syscall, Volume>;
  }
}
