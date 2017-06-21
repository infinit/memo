#include <memo/credentials/Credentials.hh>

namespace memo
{
  Credentials::Credentials(elle::serialization::SerializerIn& input)
  {
    this->serialize(input);
  }

  void
  Credentials::serialize(elle::serialization::Serializer& s)
  {}
}
