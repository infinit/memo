#ifndef INFINIT_MODEL_DOUGHNUT_PASSPORT_HH
# define INFINIT_MODEL_DOUGHNUT_PASSPORT_HH

# include <elle/Buffer.hh>
# include <elle/attribute.hh>

# include <cryptography/rsa/KeyPair.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Passport
      {
      public:
        Passport(cryptography::rsa::PublicKey user,
                 std::string network,
                 cryptography::rsa::PrivateKey const& owner);
        Passport(elle::serialization::SerializerIn& s);
        void
        serialize(elle::serialization::Serializer& s);

        bool
        verify(cryptography::rsa::PublicKey const& owner);

        ELLE_ATTRIBUTE_R(cryptography::rsa::PublicKey, user);
        ELLE_ATTRIBUTE_R(std::string, network);
        ELLE_ATTRIBUTE(elle::Buffer, signature);
      };
    }
  }
}

#endif
