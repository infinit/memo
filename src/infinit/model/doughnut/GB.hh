#ifndef INFINIT_MODEL_DOUGHNUT_GB_HH
# define INFINIT_MODEL_DOUGHNUT_GB_HH


# include <elle/serialization/fwd.hh>
# include <elle/Buffer.hh>

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/User.hh>
# include <infinit/model/blocks/ACLBlock.hh>
# include <infinit/model/blocks/GroupBlock.hh>
# include <infinit/model/doughnut/OKB.hh>
# include <infinit/model/doughnut/ACB.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class GB
        : public BaseACB<blocks::GroupBlock>
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        typedef GB Self;
        typedef BaseACB<blocks::GroupBlock> Super;
        GB(Doughnut* owner, cryptography::rsa::KeyPair master);
        GB(GB const& other, bool sealed_copy = true);
        ~GB();

      public:
        virtual
        void
        add_member(model::User const& user) override;
        virtual
        void
        remove_member(model::User const& user) override;
        virtual
        void
        add_admin(model::User const& user) override;
        virtual
        void
        remove_admin(model::User const& user) override;
        virtual
        cryptography::rsa::PublicKey
        current_public_key() override;
        virtual
        cryptography::rsa::KeyPair
        current_key() override;
        virtual
        std::vector<cryptography::rsa::KeyPair>
        all_keys() override;
        virtual
        std::vector<cryptography::rsa::PublicKey>
        all_public_keys() override;
        virtual
        std::vector<std::unique_ptr<model::User>>
        list_admins(bool ommit_names) override;
        virtual
        int
        version() override;
      protected:
        class OwnerSignature
          : public Super::OwnerSignature
        {
        public:
          OwnerSignature(GB const& block);
        protected:
          virtual
          void
          _serialize(elle::serialization::SerializerOut& s,
                     elle::Version const& v);
          ELLE_ATTRIBUTE_R(GB const&, block);
        };
        virtual
        std::unique_ptr<typename BaseOKB<blocks::GroupBlock>::OwnerSignature>
        _sign() const override;
        class DataSignature
          : public Super::DataSignature
        {
        public:
          DataSignature(GB const& block);
          virtual
          void
          serialize(elle::serialization::Serializer& s_,
                    elle::Version const& v);
          ELLE_ATTRIBUTE_R(GB const&, block);
        };
        virtual
        std::unique_ptr<Super::DataSignature>
        _data_sign() const override;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        GB(elle::serialization::SerializerIn& s,
           elle::Version const& version);
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
      private:
        void
        _serialize(elle::serialization::Serializer& s,
                   elle::Version const& version);

      /*---------.
      | Clonable |
      `---------*/
      public:
        virtual
        std::unique_ptr<blocks::Block>
        clone(bool sealed_copy) const override;
      private:
        void
        _extract_keys();
        /// The decrypted group keys.
        ELLE_ATTRIBUTE(std::vector<infinit::cryptography::rsa::KeyPair>, keys);
        /// The group public keys, for other user to give the group access.
        ELLE_ATTRIBUTE_R(std::vector<infinit::cryptography::rsa::PublicKey>,
                         public_keys);
        // Order matter for signing, hence std::map.
        typedef std::map<infinit::cryptography::rsa::PublicKey, elle::Buffer>
          AdminKeys;
        /// The group admin keys ciphered for every admin.
        ELLE_ATTRIBUTE_R(AdminKeys, admin_keys);

      public:
        typedef infinit::serialization_tag serialization_tag;

      /*----------.
      | Printable |
      `----------*/
      public:
        virtual
        void
        print(std::ostream& ouptut) const;
      };
    }
  }
}

#endif
