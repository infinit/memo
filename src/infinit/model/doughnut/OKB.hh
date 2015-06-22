#ifndef INFINIT_MODEL_DOUGHNUT_OKB_HH
# define INFINIT_MODEL_DOUGHNUT_OKB_HH

# include <cryptography/KeyPair.hh>

# include <infinit/model/blocks/MutableBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      struct OKBHeader
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        OKBHeader(cryptography::KeyPair const& keys);
        ELLE_ATTRIBUTE_R(cryptography::PublicKey, key);
        ELLE_ATTRIBUTE_R(cryptography::PublicKey, owner_key);
        ELLE_ATTRIBUTE_R(cryptography::Signature, signature);
        friend class OKB;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        void
        serialize(elle::serialization::Serializer& input);
      protected:
        OKBHeader();
      };

      class OKB
        : public OKBHeader
        , public blocks::MutableBlock
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef OKB Self;
        typedef blocks::MutableBlock Super;


      /*-------------.
      | Construction |
      `-------------*/
      public:
        OKB(cryptography::KeyPair const& keys);
        ELLE_ATTRIBUTE_R(int, version);
        ELLE_ATTRIBUTE_R(cryptography::Signature, signature);
        ELLE_ATTRIBUTE_R(cryptography::KeyPair, keys);

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        virtual
        void
        _seal() override;
        virtual
        bool
        _validate(blocks::Block const& previous) const override;
        virtual
        bool
        _validate() const override;

      private:
        elle::Buffer
        _sign() const;
        static
        Address
        _hash_address(cryptography::PublicKey const& key);

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        OKB(elle::serialization::Serializer& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input);
      };
    }
  }
}


#endif
