#ifndef INFINIT_AWS_CREDENTIALS_HH
# define INFINIT_AWS_CREDENTIALS_HH

# include <infinit/credentials/Credentials.hh>

namespace infinit
{
  class AWSCredentials
    : public Credentials
  {
  public:
    AWSCredentials(std::string account,
                   std::string access_key_id,
                   std::string secret_access_key);
    AWSCredentials(elle::serialization::SerializerIn& input);
    virtual
    ~AWSCredentials() override;

    void
    serialize(elle::serialization::Serializer& s) override;

    virtual
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

#endif
