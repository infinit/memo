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
        void create();
        cryptography::rsa::PublicKey
        public_control_key();
        cryptography::rsa::PublicKey
        current_key();
        std::vector<std::unique_ptr<model::User>>
        list_members(bool ommit_names = false);
        void
        add_member(model::User const& user);
        void
        remove_member(model::User const& user);
        std::vector<cryptography::rsa::KeyPair>
        group_keys();
      private:
        cryptography::rsa::KeyPair _control_key();
        typedef std::vector<cryptography::rsa::PublicKey> GroupPublicBlockContent;
        typedef std::vector<cryptography::rsa::KeyPair> GroupPrivateBlockContent;
        ELLE_ATTRIBUTE(Doughnut&, dht);
        ELLE_ATTRIBUTE_R(std::string, name);
      };
    }
  }
}

#endif