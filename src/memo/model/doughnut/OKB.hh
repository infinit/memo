#pragma once

#include <elle/serialization/fwd.hh>

#include <elle/reactor/BackgroundFuture.hh>

#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/model/blocks/MutableBlock.hh>
#include <memo/model/doughnut/fwd.hh>
#include <memo/serialization.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      struct OKBDontWaitForSignature {};

      template <typename Block>
      class BaseOKB;

      struct OKBHeader
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        OKBHeader(Doughnut* dht,
                  elle::cryptography::rsa::KeyPair const& keys,
                  boost::optional<elle::Buffer> salt);
        OKBHeader(OKBHeader const& other);

      /*---------.
      | Contents |
      `---------*/
      public:
        blocks::ValidationResult
        validate(Address const& address) const;
        ELLE_ATTRIBUTE_R(elle::Buffer, salt, protected);
        ELLE_ATTRIBUTE_R(std::shared_ptr<elle::cryptography::rsa::PublicKey>,
                         owner_key);
        ELLE_ATTRIBUTE_R(elle::Buffer, signature);
        ELLE_ATTRIBUTE_R(Doughnut*, doughnut, protected);
      protected:
        Address
        _hash_address() const;
        template <typename Block>
        friend class BaseOKB;
        friend class OwnerSignature;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        OKBHeader(elle::serialization::SerializerIn& input,
                  elle::Version const& version);
        void
        serialize(elle::serialization::Serializer& s, elle::Version const& v);
        static
        Address
        hash_address(Doughnut const& dht,
                     elle::cryptography::rsa::PublicKey const& key,
                     elle::Buffer const& salt);
        static
        Address
        hash_address(elle::cryptography::rsa::PublicKey const& key,
                     elle::Buffer const& salt,
                     elle::Version const& compatibility_version);
        using serialization_tag = memo::serialization_tag;
      };

      template <typename Block>
      class BaseOKB
        : public Block
        , public OKBHeader
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = BaseOKB;
        using Super = Block;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        BaseOKB(Doughnut* dht,
                elle::Buffer data = {},
                boost::optional<elle::Buffer> salt = {},
                Address owner = Address::null);
        BaseOKB(Doughnut* dht,
                elle::Buffer data,
                boost::optional<elle::Buffer> salt,
                elle::cryptography::rsa::KeyPair const& owner_keys,
                Address owner = Address::null);
        BaseOKB(BaseOKB const& other);
        ELLE_ATTRIBUTE_R(int, version, virtual, override);
      protected:
        using SignFuture = elle::reactor::BackgroundFuture<elle::Buffer>;
        ELLE_ATTRIBUTE(std::shared_ptr<SignFuture>, signature, protected);
        friend class Doughnut;
      private:
        BaseOKB(OKBHeader header,
                elle::Buffer data,
                std::shared_ptr<elle::cryptography::rsa::PrivateKey> owner_key,
                Address owner = Address::null);

      /*--------.
      | Content |
      `--------*/
      public:
        ELLE_attribute_r(elle::Buffer, data, override);

        void
        data(elle::Buffer data) override;

        void
        data(std::function<void (elle::Buffer&)> transformation) override;

        bool
        operator ==(blocks::Block const& rhs) const override;
        ELLE_ATTRIBUTE_R(std::shared_ptr<elle::cryptography::rsa::PrivateKey>,
                         owner_private_key, protected);
        ELLE_ATTRIBUTE_R(elle::Buffer, data_plain, protected);
        ELLE_ATTRIBUTE(bool, data_decrypted, protected);
      protected:
        void
        _decrypt_data() const;
        virtual
        elle::Buffer
        _decrypt_data(elle::Buffer const&) const;

      /*-----------.
      | Validation |
      `-----------*/
      protected:

        void
        _seal(boost::optional<int> version) override;
        void
        _seal_okb(boost::optional<int> version = {}, bool bump_version = true);

        blocks::ValidationResult
        _validate(Model const& model, bool writing) const override;
      protected:
        class OwnerSignature
        {
        public:
          using serialization_tag = memo::serialization_tag;
          OwnerSignature(BaseOKB<Block> const& block);
          void
          serialize(elle::serialization::Serializer& s_,
                    elle::Version const& v);
        protected:
          virtual
          void
          _serialize(elle::serialization::SerializerOut& s_,
                     elle::Version const& v);
          ELLE_ATTRIBUTE_R(BaseOKB<Block> const&, block);
        };
        virtual
        std::unique_ptr<OwnerSignature>
        _sign() const;
        void
        _decrypt() override;
        bool
        _check_signature(elle::cryptography::rsa::PublicKey const& key,
                         elle::Buffer const& signature,
                         elle::Buffer const& data,
                         std::string const& name) const;

        template <typename T>
        blocks::ValidationResult
        _validate_version(
          blocks::Block const& other_,
          int T::*member,
          int version) const;
        elle::Buffer const& signature() const;

      /*---------.
      | Clonable |
      `---------*/
      public:
        ELLE_CLONABLE();

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        BaseOKB(elle::serialization::SerializerIn& input,
                elle::Version const& version);

        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
        // Solve ambiguity between Block and OKBHedar wich both have the tag.
        using serialization_tag = memo::serialization_tag;
      private:
        void
        _serialize(elle::serialization::Serializer& input,
                   elle::Version const& version);
      };

      void
      serialize_key_hash(elle::serialization::Serializer& s,
                         elle::Version const& v,
                         elle::cryptography::rsa::PublicKey& key,
                         std::string const& field_name,
                         Doughnut* dn = nullptr);
      elle::cryptography::rsa::PublicKey
      deserialize_key_hash(elle::serialization::SerializerIn& s,
                           elle::Version const& v,
                           std::string const& field_name,
                           Doughnut* dn = nullptr);
      using OKB = BaseOKB<blocks::MutableBlock>;
    }
  }
}

#include <memo/model/doughnut/OKB.hxx>
