#include <memo/model/doughnut/User.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      User::User(elle::cryptography::rsa::PublicKey key, std::string const& name)
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
