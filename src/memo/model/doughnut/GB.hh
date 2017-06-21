#pragma once

#include <elle/serialization/fwd.hh>
#include <elle/Buffer.hh>

#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/model/User.hh>
#include <memo/model/blocks/ACLBlock.hh>
#include <memo/model/blocks/GroupBlock.hh>
#include <memo/model/doughnut/OKB.hh>
#include <memo/model/doughnut/ACB.hh>

namespace memo
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
        using Self = GB;
        using Super = BaseACB<blocks::GroupBlock>;
        GB(Doughnut* owner, elle::cryptography::rsa::KeyPair master);
        GB(GB const& other);
        ~GB() override = default;

      public:
        void
        add_member(model::User const& user) override;
        void
        remove_member(model::User const& user) override;
        void
        add_admin(model::User const& user) override;
        void
        remove_admin(model::User const& user) override;
        std::vector<std::unique_ptr<model::User>>
        list_admins(bool ommit_names) const override;
        virtual
        elle::cryptography::rsa::PublicKey
        current_public_key() const;
        virtual
        elle::cryptography::rsa::KeyPair
        current_key() const;
        virtual
        std::vector<elle::cryptography::rsa::KeyPair>
        all_keys() const;
        virtual
        std::vector<elle::cryptography::rsa::PublicKey>
        all_public_keys() const;
        virtual
        int
        group_version() const;
        std::shared_ptr<elle::cryptography::rsa::PrivateKey>
        control_key() const;

      protected:
        class OwnerSignature
          : public Super::OwnerSignature
        {
        public:
          OwnerSignature(GB const& block);
        protected:
          void
          _serialize(elle::serialization::SerializerOut& s,
                     elle::Version const& v) override;
          ELLE_ATTRIBUTE_R(GB const&, block);
        };
        std::unique_ptr<typename BaseOKB<blocks::GroupBlock>::OwnerSignature>
        _sign() const override;
        class DataSignature
          : public Super::DataSignature
        {
        public:
          DataSignature(GB const& block);

          void
          serialize(elle::serialization::Serializer& s_,
                    elle::Version const& v) override;
          ELLE_ATTRIBUTE_R(GB const&, block);
        };
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
        std::unique_ptr<blocks::Block>
        clone() const override;
      private:
        void
        _extract_keys();
        /// The decrypted group keys.
        ELLE_ATTRIBUTE(std::vector<elle::cryptography::rsa::KeyPair>, keys);
        /// The group public keys, for other user to give the group access.
        ELLE_ATTRIBUTE_R(std::vector<elle::cryptography::rsa::PublicKey>,
                         public_keys);
        // Order matter for signing, hence std::map.
        using AdminKeys
          = std::map<elle::cryptography::rsa::PublicKey, elle::Buffer>;
        /// The group admin keys ciphered for every admin.
        ELLE_ATTRIBUTE_R(AdminKeys, admin_keys);
      public:
        /// Optional group description.
        ELLE_ATTRIBUTE_rw(boost::optional<std::string>, description);

      public:
        using serialization_tag = memo::serialization_tag;

      /*----------.
      | Printable |
      `----------*/
      public:
        void
        print(std::ostream& ouptut) const override;
      };
    }
  }
}
