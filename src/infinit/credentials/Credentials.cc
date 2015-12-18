#include <infinit/credentials/Credentials.hh>

namespace infinit
{
  Credentials::Credentials(elle::serialization::SerializerIn& input)
  {
    this->serialize(input);
  }

  Credentials::~Credentials()
  {}

  void
  Credentials::serialize(elle::serialization::Serializer& s)
  {}

  std::string
  Credentials::display_name() const
  {
    return "";
  }

  std::string
  Credentials::uid() const
  {
    return "";
  }
}
