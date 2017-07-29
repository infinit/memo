#pragma once

#include <elle/cryptography/rsa/PublicKey.hh>

#include <memo/model/User.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      class User
        : public model::User
      {
      public:
        User(elle::cryptography::rsa::PublicKey key, std::string name);
        std::string name() const override;
        ELLE_ATTRIBUTE_R(elle::cryptography::rsa::PublicKey, key);
        ELLE_ATTRIBUTE(std::string, name);
      };
    }
  }
}
