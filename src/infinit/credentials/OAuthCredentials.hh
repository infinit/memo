#ifndef INFINIT_OAUTH_CREDENTIALS_HH
# define INFINIT_OAUTH_CREDENTIALS_HH

# include <infinit/credentials/Credentials.hh>

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
    virtual
    ~OAuthCredentials() override;

    void
    serialize(elle::serialization::Serializer& s) override;

    virtual
    std::string
    display_name() const override;

    virtual
    std::string
    uid() const override;

    std::string _uid;
    std::string _display_name;
    std::string token;
    std::string refresh_token;
  };
}

#endif
