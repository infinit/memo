#ifndef INFINIT_MODEL_DOUGHNUT_OKB_HH
# define INFINIT_MODEL_DOUGHNUT_OKB_HH

# include <elle/serialization/fwd.hh>

# include <cryptography/KeyPair.hh>

# include <infinit/model/blocks/MutableBlock.hh>
# include <infinit/model/doughnut/fwd.hh>

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

      /*---------.
      | Contents |
      `---------*/
      public:
        bool
        validate(Address const& address) const;
        ELLE_ATTRIBUTE_R(cryptography::PublicKey, key);
        ELLE_ATTRIBUTE_R(cryptography::PublicKey, owner_key);
        ELLE_ATTRIBUTE_R(cryptography::Signature, signature);
      protected:
        Address
        _hash_address() const;
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
        OKB(Doughnut* owner);
        ELLE_ATTRIBUTE_R(int, version);
        ELLE_ATTRIBUTE_R(cryptography::Signature, signature);
        ELLE_ATTRIBUTE_R(Doughnut*, doughnut);
        friend class Doughnut;

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
      protected:
        virtual
        void
        _sign(elle::serialization::SerializerOut& s) const;
        bool
        _check_signature(cryptography::PublicKey const& key,
                         cryptography::Signature const& signature,
                         elle::Buffer const& data,
                         std::string const& name) const;

        template <typename T>
        bool
        _validate_version(Block const& other_,
                          int T::*member,
                          int version) const;
      private:
        elle::Buffer
        _sign() const;

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

# include <infinit/model/doughnut/OKB.hxx>

#endif
