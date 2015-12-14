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
        ~GB();
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
        std::vector<std::unique_ptr<model::User>>
        list_admins(bool ommit_names) override;
        virtual
        int
        version() override;
      protected:
        virtual
        void
        _sign(elle::serialization::SerializerOut& s) const override;
        virtual
        void
        _data_sign(elle::serialization::SerializerOut& s) const override;
      public:
        GB(elle::serialization::SerializerIn& s,
           elle::Version const& version);
        void serialize(elle::serialization::Serializer& s,
                       elle::Version const& version) override;
        GB(GB const& other, bool sealed_copy = true);
        virtual
        std::unique_ptr<blocks::Block>
        clone(bool sealed_copy) const override;
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
        //due to serialization glitch (cant serialize pair
        //with non-default-constructible type,
        // use two vectors
        ELLE_ATTRIBUTE(std::vector<infinit::cryptography::rsa::PublicKey>, group_admins);
        ELLE_ATTRIBUTE(std::vector<elle::Buffer>, ciphered_master_key);

      public:
        typedef infinit::serialization_tag serialization_tag;
      };
    }
  }
}

#endif