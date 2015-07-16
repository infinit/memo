#include <infinit/model/doughnut/User.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      User::User(cryptography::rsa::PublicKey key)
        : _key(std::move(key))
      {}
    }
  }
}
