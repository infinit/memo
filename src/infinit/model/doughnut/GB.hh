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
      public:
        typedef GB Self;
        typedef BaseACB<blocks::GroupBlock> Super;

        GB(Doughnut* owner, cryptography::rsa::KeyPair master);
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
        current_key() override;
        virtual
        std::vector<cryptography::rsa::KeyPair>
        all_keys() override;
      protected:
        virtual
        void
        _sign(elle::serialization::SerializerOut& s) const override;
        virtual
        void
        _data_sign(elle::serialization::SerializerOut& s) const override;
      public:
        GB(elle::serialization::SerializerIn& s);
        void serialize(elle::serialization::Serializer& s) override;

      private:
        void
        _extract_group_keys();
        void
        _extract_master_key();
        // extracted stuff
        ELLE_ATTRIBUTE(boost::optional<infinit::cryptography::rsa::PrivateKey>, master_key);
        ELLE_ATTRIBUTE(std::vector<infinit::cryptography::rsa::KeyPair>, group_keys);
        // stored stuff
        ELLE_ATTRIBUTE(std::vector<infinit::cryptography::rsa::PublicKey>, group_public_keys);
        // key is serialized public key because of a serialization glitch
        typedef
        std::pair<elle::Buffer, elle::Buffer>
        BufferPair;
        typedef
        std::vector<BufferPair>
        CipheredMasterKey;

        ELLE_ATTRIBUTE(CipheredMasterKey, ciphered_master_key);
      };
    }
  }
}

#endif