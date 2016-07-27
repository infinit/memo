#ifndef INFINIT_MODEL_DOUGHNUT_GROUP_HH
# define INFINIT_MODEL_DOUGHNUT_GROUP_HH

#include <reactor/storage.hh>
#include <cryptography/rsa/KeyPair.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/GB.hh>


namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class Group
        : public elle::Printable
      {
      public:
        Group(Doughnut& dht, std::string const& name);
        Group(Doughnut& dht, cryptography::rsa::PublicKey);
        ~Group();
        void
        create();
        cryptography::rsa::PublicKey
        public_control_key() const;
        GB&
        block() const;
        cryptography::rsa::PublicKey
        current_public_key() const;
        cryptography::rsa::KeyPair
        current_key() const;
        int
        version() const;
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
        void
        destroy();
        void
        print(std::ostream& o) const;
      private:
        void _stack_push();
        cryptography::rsa::KeyPair _control_key();
        typedef std::vector<cryptography::rsa::PublicKey> GroupPublicBlockContent;
        typedef std::vector<cryptography::rsa::KeyPair> GroupPrivateBlockContent;
        ELLE_ATTRIBUTE(Doughnut&, dht);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE(boost::optional<cryptography::rsa::PublicKey>,
                       public_control_key);
        ELLE_ATTRIBUTE(std::unique_ptr<GB>, block);
        static reactor::LocalStorage<std::vector<cryptography::rsa::PublicKey>> _stack;
      };

      class GroupConflictResolver : public ConflictResolver
      {
      public:
        enum class Action
        {
          add_member,
          remove_member,
          add_admin,
          remove_admin,
        };
        GroupConflictResolver(Action action, model::User const& user);
        GroupConflictResolver(GroupConflictResolver&& b);
        GroupConflictResolver(elle::serialization::SerializerIn& s,
                              elle::Version const& v);
        ~GroupConflictResolver();
        std::unique_ptr<blocks::Block>
        operator() (blocks::Block& block,
                    blocks::Block& current,
                    model::StoreMode mode) override;
        void serialize(elle::serialization::Serializer& s,
                       elle::Version const& v) override;

        std::string
        description() const override;

        Action _action;
        std::unique_ptr<cryptography::rsa::PublicKey> _key;
        std::string _name;
        typedef infinit::serialization_tag serialization_tag;
      };
    }
  }
}

namespace std
{
  std::ostream&
  operator << (std::ostream& out,
               infinit::model::doughnut::GroupConflictResolver::Action action);
}
#endif
