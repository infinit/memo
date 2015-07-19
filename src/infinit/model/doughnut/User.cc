#include <infinit/model/doughnut/User.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      User::User(cryptography::rsa::PublicKey key, std::string const& name)
        : _key(std::move(key))
        , _name(name)
      {}
      std::string User::name()
      {
        return _name;
      }
    }
  }
}
