#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/Passport.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Passport::Passport(cryptography::rsa::PublicKey user,
                         std::string network,
                         cryptography::rsa::PrivateKey const& owner)
        : _user(std::move(user))
        , _network(std::move(network))
        , _signature()
      {
        elle::Buffer fingerprint;
        {
          elle::IOStream output(fingerprint.ostreambuf());
          elle::serialization::json::SerializerOut s(output, false);
          s.serialize("user", this->_user);
          s.serialize("network", this->_network);
        }
        this->_signature = owner.sign(fingerprint);
      }

      Passport::Passport(elle::serialization::SerializerIn& s)
        : _user(s.deserialize<cryptography::rsa::PublicKey>("user"))
        , _network(s.deserialize<std::string>("network"))
        , _signature(s.deserialize<elle::Buffer>("signature"))
      {}

      Passport::~Passport()
      {}

      void
      Passport::serialize(elle::serialization::Serializer& s)
      {
        s.serialize("user", this->_user);
        s.serialize("network", this->_network);
        s.serialize("signature", this->_signature);
      }

      bool
      Passport::verify(cryptography::rsa::PublicKey const& owner)
      {
        elle::Buffer fingerprint;
        {
          elle::IOStream output(fingerprint.ostreambuf());
          elle::serialization::json::SerializerOut s(output, false);
          s.serialize("user", this->_user);
          s.serialize("network", this->_network);
        }
        return owner.verify(this->_signature, fingerprint);
      }
    }
  }
}
