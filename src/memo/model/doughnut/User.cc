#include <memo/model/doughnut/User.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      User::User(elle::cryptography::rsa::PublicKey key, std::string name)
        : _key(std::move(key))
        , _name(std::move(name))
      {}

      std::string
      User::name() const
      {
        return _name;
      }
    }
  }
}
