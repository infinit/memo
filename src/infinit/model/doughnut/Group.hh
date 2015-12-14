#ifndef INFINIT_MODEL_DOUGHNUT_GROUP_HH
# define INFINIT_MODEL_DOUGHNUT_GROUP_HH

#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/doughnut/Doughnut.hh>


namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Group
      {
      public:
        Group(Doughnut& dht, std::string const& name);
        Group(Doughnut& dht, cryptography::rsa::PublicKey);
        void create();
        cryptography::rsa::PublicKey
        public_control_key();
        cryptography::rsa::PublicKey
        current_public_key();
        cryptography::rsa::KeyPair
        current_key();
        int
        version();
        std::vector<std::unique_ptr<model::User>>
        list_members(bool ommit_names = false);
        std::vector<std::unique_ptr<model::User>>
        list_admins(bool ommit_names = false);
        void
        add_member(model::User const& user);
        void
        add_member(elle::Buffer const& user_data);
        void
        remove_member(model::User const& user);
        void
        remove_member(elle::Buffer const& user_data);
        void
        add_admin(model::User const& user);
        void
        add_admin(elle::Buffer const& user_data);
        void
        remove_admin(model::User const& user);
        void
        remove_admin(elle::Buffer const& user_data);
        std::vector<cryptography::rsa::KeyPair>
        group_keys();
        std::vector<cryptography::rsa::PublicKey>
        group_public_keys();
      private:
        cryptography::rsa::KeyPair _control_key();
        typedef std::vector<cryptography::rsa::PublicKey> GroupPublicBlockContent;
        typedef std::vector<cryptography::rsa::KeyPair> GroupPrivateBlockContent;
        ELLE_ATTRIBUTE(Doughnut&, dht);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE(boost::optional<cryptography::rsa::PublicKey>, public_control_key);
      };
    }
  }
}

#endif