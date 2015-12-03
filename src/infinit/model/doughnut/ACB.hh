#ifndef INFINIT_MODEL_DOUGHNUT_ACB_HH
# define INFINIT_MODEL_DOUGHNUT_ACB_HH

# include <thread>

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
      struct ACBDontWaitForSignature {};
      class ACB
        : public BaseOKB<blocks::ACLBlock>
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef ACB Self;
        typedef BaseOKB<blocks::ACLBlock> Super;
        struct ACLEntry
        {
          infinit::cryptography::rsa::PublicKey key;
          bool read;
          bool write;
          elle::Buffer token;

          ACLEntry(infinit::cryptography::rsa::PublicKey key_,
                   bool read_,
                   bool write_,
                   elle::Buffer token_);
          ACLEntry(ACLEntry const& other);
          ACLEntry(elle::serialization::SerializerIn& s);
          void serialize(elle::serialization::Serializer& s);
          ACLEntry&
          operator =(ACLEntry&& other) = default;

          bool operator == (ACLEntry const& b) const;

          typedef infinit::serialization_tag serialization_tag;
          static ACLEntry deserialize(elle::serialization::SerializerIn& s);
        };

      /*-------------.
      | Construction |
      `-------------*/
      public:
        ACB(Doughnut* owner);
        ACB(ACB const& other, bool sealed_copy = true);
        ~ACB();
        ELLE_ATTRIBUTE_R(int, editor);
        ELLE_ATTRIBUTE(elle::Buffer, owner_token);
        ELLE_ATTRIBUTE(bool, acl_changed);
        ELLE_ATTRIBUTE(std::vector<ACLEntry>, acl_entries);
        ELLE_ATTRIBUTE_R(int, data_version);
        ELLE_ATTRIBUTE(reactor::BackgroundFuture<elle::Buffer>, data_signature);
      protected:
        elle::Buffer const& data_signature() const;

      /*-------.
      | Clone  |
      `-------*/
      public:
        virtual
        std::unique_ptr<blocks::Block>
        clone(bool sealed_copy) const override;

      /*--------.
      | Content |
      `--------*/
      protected:
        virtual
        int
        version() const override;
        virtual
        elle::Buffer
        _decrypt_data(elle::Buffer const& data) const override;
        void
        _stored() override;
        virtual
        bool
        operator ==(Block const& rhs) const override;

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
        virtual
        void
        _copy_permissions(ACLBlock& to) override;
        virtual
        std::vector<Entry>
        _list_permissions(bool ommit_names) override;

      /*-----------.
      | Validation |
      `-----------*/
      protected:
        virtual
        blocks::ValidationResult
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
        ACB(elle::serialization::SerializerIn& input);
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
