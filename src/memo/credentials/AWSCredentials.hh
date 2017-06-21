#pragma once

#include <memo/credentials/Credentials.hh>

namespace memo
{
  class AWSCredentials
    : public Credentials
  {
  public:
    AWSCredentials(std::string account,
                   std::string access_key_id,
                   std::string secret_access_key);
    AWSCredentials(elle::serialization::SerializerIn& input);

    void
    serialize(elle::serialization::Serializer& s) override;

    std::string
    display_name() const override;

    virtual
    std::string
    uid() const override;

    std::string account;
    std::string access_key_id;
    std::string secret_access_key;
  };
}
