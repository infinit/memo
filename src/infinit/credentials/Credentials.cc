#include <infinit/credentials/Credentials.hh>

namespace infinit
{
  Credentials::Credentials(elle::serialization::SerializerIn& input)
  {
    this->serialize(input);
  }

  void
  Credentials::serialize(elle::serialization::Serializer& s)
  {}
}
