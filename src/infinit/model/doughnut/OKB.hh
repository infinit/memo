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
      template <typename Block>
      class BaseOKB;

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
        template <typename Block>
        friend class BaseOKB;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        void
        serialize(elle::serialization::Serializer& input);
      protected:
        OKBHeader();
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
        BaseOKB(Doughnut* owner);
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
        _validate_version(blocks::Block const& other_,
                          int T::*member,
                          int version) const;
      private:
        elle::Buffer
        _sign() const;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        BaseOKB(elle::serialization::Serializer& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input);
      };

      typedef BaseOKB<blocks::MutableBlock> OKB;
    }
  }
}

# include <infinit/model/doughnut/OKB.hxx>

#endif
