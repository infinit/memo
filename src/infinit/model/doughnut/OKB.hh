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
        OKBHeader(cryptography::rsa::KeyPair const& keys,
                  boost::optional<elle::Buffer> salt);
        OKBHeader(OKBHeader const& other);

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
      protected:
        Address
        _hash_address() const;
        template <typename Block>
        friend class BaseOKB;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        OKBHeader(std::shared_ptr<cryptography::rsa::PublicKey> keys,
                  elle::Buffer salt,
                  elle::Buffer signature);
        void
        serialize(elle::serialization::Serializer& s);
        static
        Address
        hash_address(cryptography::rsa::PublicKey& key,
                     elle::Buffer const& salt);
        typedef infinit::serialization_tag serialization_tag;
      };

      template <typename Block>
      class BaseOKB
        : public OKBHeader
        , public Block
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
                boost::optional<elle::Buffer> salt = {},
                boost::optional<cryptography::rsa::KeyPair> kp = {}
                );
        BaseOKB(BaseOKB const& other, bool sealed_copy = true);
        ELLE_ATTRIBUTE(int, version);
        ELLE_ATTRIBUTE(reactor::BackgroundFuture<elle::Buffer>, signature, protected);
        ELLE_ATTRIBUTE_R(Doughnut*, doughnut);
        friend class Doughnut;

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
        ELLE_ATTRIBUTE_RX(boost::optional<cryptography::rsa::KeyPair>, keys);
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
        _seal() override;
        void
        _seal_okb(bool bump_version = true);
        virtual
        blocks::ValidationResult
        _validate() const override;
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
        virtual
        std::unique_ptr<blocks::Block>
        clone(bool seal_copy) const override;

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
        class SerializationContent;
        BaseOKB(SerializationContent input);
        void
        _serialize(elle::serialization::Serializer& input,
                   elle::Version const& version);
      };

      typedef BaseOKB<blocks::MutableBlock> OKB;
    }
  }
}

# include <infinit/model/doughnut/OKB.hxx>

#endif
