#include <memo/cli/Doctor.hh>
#include <memo/cli/Object.hxx>

namespace memo
{
  namespace cli
  {
    template class Object<Doctor>;
    template class Object<Doctor::Log, Doctor>;
  }
}
