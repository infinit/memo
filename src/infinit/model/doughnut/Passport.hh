#ifndef INFINIT_MODEL_DOUGHNUT_PASSPORT_HH
# define INFINIT_MODEL_DOUGHNUT_PASSPORT_HH

# include <elle/Buffer.hh>
# include <elle/attribute.hh>

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/serialization.hh>

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
                 cryptography::rsa::PrivateKey const& owner,
                 bool allow_write = true,
                 bool allow_storage = true);
        Passport(elle::serialization::SerializerIn& s);
        ~Passport();
        void
        serialize(elle::serialization::Serializer& s);
        typedef infinit::serialization_tag serialization_tag;

        bool
        verify(cryptography::rsa::PublicKey const& owner);

        ELLE_ATTRIBUTE_R(cryptography::rsa::PublicKey, user);
        ELLE_ATTRIBUTE_R(std::string, network);
        ELLE_ATTRIBUTE(elle::Buffer, signature);
        ELLE_ATTRIBUTE_R(bool, allow_write);
        ELLE_ATTRIBUTE_R(bool, allow_storage);
      };
    }
  }
}

#endif
