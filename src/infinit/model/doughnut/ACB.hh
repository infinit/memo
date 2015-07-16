#ifndef INFINIT_MODEL_DOUGHNUT_ACB_HH
# define INFINIT_MODEL_DOUGHNUT_ACB_HH

# include <elle/serialization/fwd.hh>

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/User.hh>
# include <infinit/model/blocks/ACLBlock.hh>
# include <infinit/model/doughnut/OKB.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class ACB
        : public BaseOKB<blocks::ACLBlock>
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef ACB Self;
        typedef BaseOKB<blocks::ACLBlock> Super;

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
        ELLE_ATTRIBUTE(elle::Buffer, data_signature);

      /*--------.
      | Content |
      `--------*/
      protected:
        virtual
        elle::Buffer
        _decrypt_data(elle::Buffer const& data) const;

      /*------------.
      | Permissions |
      `------------*/
      public:
        virtual
        void
        set_permissions(cryptography::rsa::PublicKey const& key,
                        bool read,
                        bool write);
      protected:
        virtual
        void
        _set_permissions(model::User const& key,
                         bool read,
                         bool write) override;

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
