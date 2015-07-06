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
        ACB(Doughnut* owner);
        ELLE_ATTRIBUTE_R(int, editor);
        ELLE_ATTRIBUTE(elle::Buffer, owner_token);
        ELLE_ATTRIBUTE_R(Address, acl);
        ELLE_ATTRIBUTE(bool, acl_changed);
        ELLE_ATTRIBUTE(int, data_version);
        ELLE_ATTRIBUTE(cryptography::Signature, data_signature);

      /*------------.
      | Permissions |
      `------------*/
      public:
        void
        set_permissions(cryptography::PublicKey const& key,
                        bool read,
                        bool write);

      /*-----------.
      | Validation |
      `-----------*/
      protected:
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
      private:
        elle::Buffer
        _data_sign() const;

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
