#pragma once

#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/model/User.hh>
#include <memo/model/blocks/ACLBlock.hh>

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      class GroupBlock
        : public ACLBlock
        , private InstanceTracker<GroupBlock>
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = GroupBlock;
        using Super = ACLBlock;
        static char const* type;

        GroupBlock(GroupBlock const& other) = default;

      protected:
        GroupBlock(Address address,
                   elle::Buffer data = {},
                   Address owner = Address::null);
        friend class memo::model::Model;

      public:
        virtual
        void
        add_member(model::User const& user);
        virtual
        void
        remove_member(model::User const& user);
        virtual
        void
        add_admin(model::User const& user);
        virtual
        void
        remove_admin(model::User const& user);
        virtual
        std::vector<std::unique_ptr<User>>
        list_admins(bool ommit_names) const;

      public:
        GroupBlock(elle::serialization::Serializer& input,
                   elle::Version const& version);
      };
    }
  }
}
