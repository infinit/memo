#ifndef INFINIT_MODEL_DOUGHNUT_OKB_HH
# define INFINIT_MODEL_DOUGHNUT_OKB_HH

# include <elle/serialization/fwd.hh>

# include <reactor/BackgroundFuture.hh>

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/blocks/MutableBlock.hh>
# include <infinit/model/doughnut/fwd.hh>
# include <infinit/serialization.hh>

namespace infinit
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
                  cryptography::rsa::KeyPair const& keys,
                  boost::optional<elle::Buffer> salt);
        OKBHeader(OKBHeader const& other);
        ELLE_ATTRIBUTE_R(Doughnut*, dht);

      /*---------.
      | Contents |
      `---------*/
      public:
        blocks::ValidationResult
        validate(Address const& address) const;
        ELLE_ATTRIBUTE_R(elle::Buffer, salt);
        ELLE_ATTRIBUTE_R(std::shared_ptr<cryptography::rsa::PublicKey>,
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
        serialize(elle::serialization::Serializer& s);
        static
        Address
        hash_address(Doughnut const& dht,
                     cryptography::rsa::PublicKey const& key,
                     elle::Buffer const& salt);
        static
        Address
        hash_address(cryptography::rsa::PublicKey const& key,
                     elle::Buffer const& salt,
                     elle::Version const& compatibility_version);
        typedef infinit::serialization_tag serialization_tag;
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
        typedef BaseOKB<Block> Self;
        typedef Block Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        BaseOKB(Doughnut* owner,
                elle::Buffer data = {},
                boost::optional<elle::Buffer> salt = {});
        BaseOKB(Doughnut* owner,
                elle::Buffer data,
                boost::optional<elle::Buffer> salt,
                cryptography::rsa::KeyPair const& owner_keys);
        BaseOKB(BaseOKB const& other);
        ELLE_ATTRIBUTE(int, version);
      protected:
        typedef reactor::BackgroundFuture<elle::Buffer> SignFuture;
        ELLE_ATTRIBUTE(std::shared_ptr<SignFuture>, signature, protected);
        friend class Doughnut;
      private:
        BaseOKB(OKBHeader header,
                elle::Buffer data,
                std::shared_ptr<cryptography::rsa::PrivateKey> owner_key);

      /*--------.
      | Content |
      `--------*/
      public:
        virtual
        int
        version() const override;
        virtual
        elle::Buffer const&
        data() const override;
        virtual
        void
        data(elle::Buffer data) override;
        virtual
        void
        data(std::function<void (elle::Buffer&)> transformation) override;
        virtual
        bool
        operator ==(blocks::Block const& rhs) const override;
        ELLE_ATTRIBUTE_R(std::shared_ptr<cryptography::rsa::PrivateKey>,
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
        virtual
        void
        _seal(boost::optional<int> version) override;
        void
        _seal_okb(boost::optional<int> version = {}, bool bump_version = true);
        virtual
        blocks::ValidationResult
        _validate(Model const& model) const override;
      protected:
        class OwnerSignature
        {
        public:
          typedef infinit::serialization_tag serialization_tag;
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
        bool
        _check_signature(cryptography::rsa::PublicKey const& key,
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
        virtual
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
        // Solve ambiguity between Block and OKBHedar wich both have the tag.
        typedef infinit::serialization_tag serialization_tag;
      private:
        void
        _serialize(elle::serialization::Serializer& input,
                   elle::Version const& version);
      };

      void
      serialize_key_hash(elle::serialization::Serializer& s,
                         elle::Version const& v,
                         cryptography::rsa::PublicKey& key,
                         std::string const& field_name,
                         Doughnut* dn = nullptr);
      cryptography::rsa::PublicKey
      deserialize_key_hash(elle::serialization::SerializerIn& s,
                           elle::Version const& v,
                           std::string const& field_name,
                           Doughnut* dn = nullptr);
      typedef BaseOKB<blocks::MutableBlock> OKB;
    }
  }
}

# include <infinit/model/doughnut/OKB.hxx>

#endif
