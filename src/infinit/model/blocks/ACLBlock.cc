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
      ACLBlock::_set_permissions(User const&, bool, bool)
      {
        // FIXME: what do ?
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
    }
  }
}
