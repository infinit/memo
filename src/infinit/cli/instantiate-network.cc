#include <infinit/cli/Network.hh>
#include <infinit/cli/Object.hxx>

namespace infinit
{
  namespace cli
  {
    template class Object<Network>;
    template class Object<Network::Block, Network>;
  }
}
