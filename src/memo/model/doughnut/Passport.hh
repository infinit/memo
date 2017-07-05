#pragma once

#include <elle/Buffer.hh>
#include <elle/attribute.hh>

#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/serialization.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      class Passport
      {
      public:
        Passport(elle::cryptography::rsa::PublicKey user,
                 std::string network,
                 elle::cryptography::rsa::KeyPair const& owner,
                 bool store_certifier = false,
                 bool allow_write = true,
                 bool allow_storage = true,
                 bool allow_sign = false);
        Passport(elle::serialization::SerializerIn& s);
        ~Passport();
        void
        serialize(elle::serialization::Serializer& s);
        using serialization_tag = memo::serialization_tag;

        bool
        verify(elle::cryptography::rsa::PublicKey const& owner) const;

        ELLE_ATTRIBUTE_R(elle::cryptography::rsa::PublicKey, user);
        ELLE_ATTRIBUTE_R(std::string, network);
        ELLE_ATTRIBUTE(elle::Buffer, signature);
        ELLE_ATTRIBUTE_R(boost::optional<elle::cryptography::rsa::PublicKey>, certifier);
        ELLE_ATTRIBUTE_R(bool, allow_write);
        ELLE_ATTRIBUTE_R(bool, allow_storage);
        ELLE_ATTRIBUTE_R(bool, allow_sign);
      };
    }
  }
}

