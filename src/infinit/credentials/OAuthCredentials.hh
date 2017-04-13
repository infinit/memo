#pragma once

#include <infinit/credentials/Credentials.hh>

namespace infinit
{
  class OAuthCredentials
    : public Credentials
  {
  public:
    OAuthCredentials(std::string uid,
                     std::string display_name,
                     std::string token,
                     std::string refresh_token);
    OAuthCredentials(elle::serialization::SerializerIn& input);

    void
    serialize(elle::serialization::Serializer& s) override;

    std::string
    display_name() const override;

    std::string
    uid() const override;

    std::string _uid;
    std::string _display_name;
    std::string token;
    std::string refresh_token;
  };
}
