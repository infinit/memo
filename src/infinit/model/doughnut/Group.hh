#pragma once

#include <elle/reactor/storage.hh>
#include <elle/cryptography/rsa/KeyPair.hh>

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
        Group(Doughnut& dht, elle::cryptography::rsa::PublicKey);
        ~Group();
        void
        create();
        elle::cryptography::rsa::PublicKey
        public_control_key() const;
        GB const&
        block() const;
        GB&
        block();
        elle::cryptography::rsa::PublicKey
        current_public_key() const;
        elle::cryptography::rsa::KeyPair
        current_key() const;
        int
        version() const;
        std::vector<std::unique_ptr<model::User>>
        list_members(bool omit_names = false);
        std::vector<std::unique_ptr<model::User>>
        list_admins(bool omit_names = false);
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
        std::vector<elle::cryptography::rsa::KeyPair>
        group_keys();
        std::vector<elle::cryptography::rsa::PublicKey>
        group_public_keys();
        void
        destroy();
        void
        print(std::ostream& o) const override;
        ELLE_ATTRIBUTE_rw(boost::optional<std::string>, description);
      private:
        void _stack_push();
        elle::cryptography::rsa::KeyPair _control_key();
        using GroupPublicBlockContent = std::vector<elle::cryptography::rsa::PublicKey>;
        using GroupPrivateBlockContent = std::vector<elle::cryptography::rsa::KeyPair>;
        ELLE_ATTRIBUTE(Doughnut&, dht);
        ELLE_ATTRIBUTE_R(std::string, name);
        ELLE_ATTRIBUTE(boost::optional<elle::cryptography::rsa::PublicKey>,
                       public_control_key);
        ELLE_ATTRIBUTE(std::unique_ptr<GB>, block);
        static elle::reactor::LocalStorage<std::vector<elle::cryptography::rsa::PublicKey>> _stack;
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
          set_description,
        };
        GroupConflictResolver(Action action, model::User const& user);
        GroupConflictResolver(Action action,
                              boost::optional<std::string> description);
        GroupConflictResolver(GroupConflictResolver&& b);
        GroupConflictResolver(elle::serialization::SerializerIn& s,
                              elle::Version const& v);
        ~GroupConflictResolver() override;
        std::unique_ptr<blocks::Block>
        operator() (blocks::Block& block,
                    blocks::Block& current) override;
        void serialize(elle::serialization::Serializer& s,
                       elle::Version const& v) override;

        std::string
        description() const override;

        Action _action;
        std::unique_ptr<elle::cryptography::rsa::PublicKey> _key;
        boost::optional<std::string> _name;
        boost::optional<std::string> _description;
        using serialization_tag = infinit::serialization_tag;
      };

      std::ostream&
      operator << (std::ostream& out, GroupConflictResolver::Action action);
    }
  }
}
