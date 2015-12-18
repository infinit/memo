#include <infinit/credentials/OAuthCredentials.hh>

namespace infinit
{
  OAuthCredentials::OAuthCredentials(std::string uid,
                                     std::string display_name,
                                     std::string token,
                                     std::string refresh_token)
    : Credentials()
    , _uid(std::move(uid))
    , _display_name(std::move(display_name))
    , token(std::move(token))
    , refresh_token(std::move(refresh_token))
  {}

  OAuthCredentials::OAuthCredentials(elle::serialization::SerializerIn& input)
    : Credentials()
  {
    this->serialize(input);
  }

  OAuthCredentials::~OAuthCredentials()
  {}

  void
  OAuthCredentials::serialize(elle::serialization::Serializer& s)
  {
    Credentials::serialize(s);
    s.serialize("uid", this->_uid);
    s.serialize("display_name", this->_display_name);
    s.serialize("token", this->token);
    s.serialize("refresh_token", this->refresh_token);
  }

  std::string
  OAuthCredentials::display_name() const
  {
    return this->_display_name;
  }

  std::string
  OAuthCredentials::uid() const
  {
    return this->_uid;
  }

  static const elle::serialization::Hierarchy<Credentials>::
    Register<OAuthCredentials>_register_OAuthCredentials("oauth_credentials");
}
