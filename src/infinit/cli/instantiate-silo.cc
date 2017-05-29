#include <infinit/cli/Silo.hh>
#include <infinit/cli/Object.hxx>

namespace infinit
{
  namespace cli
  {
    // Instantiate
    template class Object<Silo, Memo>;
    template class Object<Silo::Create, Silo>;
  }
}
