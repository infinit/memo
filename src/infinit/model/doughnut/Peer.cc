#include <infinit/model/doughnut/Peer.hh>


namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Peer::Peer(Address id)
        : _id(std::move(id))
      {}

      Peer::~Peer()
      {}
    }
  }
}
