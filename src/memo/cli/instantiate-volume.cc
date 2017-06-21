#include <memo/cli/Volume.hh>
#include <memo/cli/Object.hxx>

namespace memo
{
  namespace cli
  {
    template class Object<Volume>;
    template class Object<Volume::Syscall, Volume>;
  }
}
