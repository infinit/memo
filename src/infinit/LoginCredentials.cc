#include <infinit/LoginCredentials.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  LoginCredentials::LoginCredentials(std::string const& name,
                                     std::string const& password_hash,
                                     std::string const& password)
    : name(name)
    , password_hash(password_hash)
    , password(password)
  {}
}
