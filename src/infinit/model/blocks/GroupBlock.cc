#include <infinit/model/blocks/GroupBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      char const* GroupBlock::type = "group";

      GroupBlock::GroupBlock(Address a, elle::Buffer data)
        : Super(a, data)
      {}

      GroupBlock::GroupBlock(elle::serialization::Serializer& input,
                             elle::Version const& version)
        : Super(input, version)
      {}

      void
      GroupBlock::add_member(model::User const& user)
      {
        elle::err("Not implemented");
      }

      void
      GroupBlock::remove_member(model::User const& user)
      {
        elle::err("Not implemented");
      }

      void
      GroupBlock::add_admin(model::User const& user)
      {
        elle::err("Not implemented");
      }

      void
      GroupBlock::remove_admin(model::User const& user)
      {
        elle::err("Not implemented");
      }

      std::vector<std::unique_ptr<User>>
      GroupBlock::list_admins(bool ommit_names) const
      {
        elle::err("Not implemented");
      }
    }
  }
}
