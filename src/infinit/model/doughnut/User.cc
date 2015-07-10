#include <infinit/model/doughnut/User.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      User::User(cryptography::PublicKey key)
        : _key(std::move(key))
      {}
    }
  }
}
