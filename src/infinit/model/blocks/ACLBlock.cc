#include <elle/log.hh>

#include <infinit/model/blocks/ACLBlock.hh>

ELLE_LOG_COMPONENT("infinit.model.blocks.ACLBlock");

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      /*-------------.
      | Construction |
      `-------------*/

      ACLBlock::ACLBlock(Address address)
        : Super(address)
      {}

      ACLBlock::ACLBlock(Address address, elle::Buffer data)
        : Super(address, data)
      {}

      ACLBlock::ACLBlock(ACLBlock const& other)
        : Super(other)
      {}

      /*-------.
      | Clone  |
      `-------*/
      std::unique_ptr<Block>
      ACLBlock::clone(bool) const
      {
        return std::unique_ptr<Block>(new ACLBlock(*this));
      }

      /*------------.
      | Permissions |
      `------------*/

      void
      ACLBlock::set_permissions(User const& user,
                                bool read,
                                bool write)
      {
        ELLE_TRACE_SCOPE("%s: set permissions for %f: read = %s, write = %s",
                         *this, user, read, write);
        this->_set_permissions(user, read, write);
      }

      void
      ACLBlock::copy_permissions(ACLBlock& to)
      {
        ELLE_TRACE_SCOPE("%s: copy permissions to %s", *this, to);
        this->_copy_permissions(to);
      }

      std::vector<ACLBlock::Entry>
      ACLBlock::list_permissions(bool ommit_names)
      {
        ELLE_TRACE_SCOPE("%s: list permissions", *this);
        return this->_list_permissions(ommit_names);
      }

      void
      ACLBlock::_set_permissions(User const&, bool, bool)
      {
        // FIXME: what do ?
      }

      void
      ACLBlock::_copy_permissions(ACLBlock& to)
      {
      }

      std::vector<ACLBlock::Entry>
      ACLBlock::_list_permissions(bool)
      {
        return {};
      }

      /*--------------.
      | Serialization |
      `--------------*/

      ACLBlock::ACLBlock(elle::serialization::Serializer& input)
        : Super(input)
      {
        this->_serialize(input);
      }

      void
      ACLBlock::serialize(elle::serialization::Serializer& s)
      {
        this->Super::serialize(s);
        this->_serialize(s);
      }

      void
      ACLBlock::_serialize(elle::serialization::Serializer&)
      {}

      static const elle::serialization::Hierarchy<Block>::
      Register<ACLBlock> _register_serialization("acl");
    }
  }
}
