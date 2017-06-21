#pragma once

#include <elle/Error.hh>
#include <elle/attribute.hh>

#include <memo/model/Address.hh>

namespace memo
{
  namespace model
  {
    class MissingBlock
      : public elle::Error
    {
    public:
      using Super = elle::Error;
      MissingBlock(Address address);
      MissingBlock(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const&) override;
      ELLE_ATTRIBUTE_R(Address, address);
    };
  }
}
