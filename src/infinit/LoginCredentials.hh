#pragma once

#include <das/serializer.hh>

#include <infinit/symbols.hh>

namespace infinit
{
  struct LoginCredentials
  {
    LoginCredentials(std::string const& name,
                     std::string const& password_hash,
                     std::string const& password = "");
    // LoginCredentials(elle::serialization::SerializerIn& s);

    std::string name;
    std::string password_hash;
    std::string password;

    using Model = das::Model<
      LoginCredentials,
      decltype(elle::meta::list(
                 infinit::symbols::name,
                 infinit::symbols::password_hash,
                 infinit::symbols::password))>;
  };
}

DAS_SERIALIZE(infinit::LoginCredentials);
