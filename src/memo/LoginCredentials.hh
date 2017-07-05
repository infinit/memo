#pragma once

#include <elle/das/serializer.hh>

#include <memo/symbols.hh>

namespace memo
{
  struct LoginCredentials
  {
    LoginCredentials(std::string const& name,
                     std::string const& password_hash,
                     std::string const& password = "");

    std::string name;
    std::string password_hash;
    std::string password;

    using Model = elle::das::Model<
      LoginCredentials,
      decltype(elle::meta::list(
                 memo::symbols::name,
                 memo::symbols::password_hash,
                 memo::symbols::password))>;
  };
}

ELLE_DAS_SERIALIZE(memo::LoginCredentials);
