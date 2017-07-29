#include <memo/cli/Silo.hh>
#include <memo/cli/Object.hxx>

namespace memo
{
  namespace cli
  {
    template class Object<Silo>;
    template class Object<Silo::Create, Silo>;
  }
}
