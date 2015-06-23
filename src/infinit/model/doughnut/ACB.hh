#ifndef INFINIT_MODEL_DOUGHNUT_ACB_HH
# define INFINIT_MODEL_DOUGHNUT_ACB_HH

# include <elle/serialization/fwd.hh>

# include <cryptography/KeyPair.hh>

# include <infinit/model/blocks/MutableBlock.hh>
# include <infinit/model/doughnut/OKB.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class ACL
      {
      public:
        ACL();
        ELLE_ATTRIBUTE_R(Address, contents);
        ELLE_ATTRIBUTE_R(int, version);
        ELLE_ATTRIBUTE_R(cryptography::Signature, signature);
        friend class ACB;
        void
        serialize(elle::serialization::Serializer& s);
      };

      class ACB
        : public OKB
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef ACB Self;
        typedef OKB Super;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        ACB(cryptography::KeyPair const& keys);
        ELLE_ATTRIBUTE_R(int, editor);
        ELLE_ATTRIBUTE(elle::Buffer, owner_token);
        ELLE_ATTRIBUTE_R(ACL, acl);

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        // virtual
        // void
        // _seal() override;
        virtual
        bool
        _validate(blocks::Block const& previous) const override;
        virtual
        bool
        _validate() const override;
        virtual
        void
        _seal() override;
        virtual
        void
        _sign(elle::serialization::SerializerOut& s) const override;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        ACB(elle::serialization::Serializer& input);
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
