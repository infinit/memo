#include <infinit/credentials/AWSCredentials.hh>

namespace infinit
{
  AWSCredentials::AWSCredentials(std::string account,
                                 std::string access_key_id,
                                 std::string secret_access_key)
    : Credentials()
    , account(std::move(account))
    , access_key_id(std::move(access_key_id))
    , secret_access_key(std::move(secret_access_key))
  {}

  AWSCredentials::AWSCredentials(elle::serialization::SerializerIn& input)
    : Credentials()
  {
    this->serialize(input);
  }

  AWSCredentials::~AWSCredentials()
  {}

  void
  AWSCredentials::serialize(elle::serialization::Serializer& s)
  {
    Credentials::serialize(s);
    s.serialize("account", this->account);
    s.serialize("access_key_id", this->access_key_id);
    s.serialize("secret_access_key", this->secret_access_key);
  }

  std::string
  AWSCredentials::display_name() const
  {
    return this->account;
  }

  std::string
  AWSCredentials::uid() const
  {
    return this->access_key_id;
  }

  static const elle::serialization::Hierarchy<Credentials>::
    Register<AWSCredentials>_register_AWSCredentials("aws_credentials");
}
