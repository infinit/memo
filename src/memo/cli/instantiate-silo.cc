#include <memo/cli/Silo.hh>
#include <memo/cli/Object.hxx>

namespace memo
{
  namespace cli
  {
    // Instantiate
    template class Object<Silo, Memo>;
    template class Object<Silo::Create, Silo>;
  }
}
