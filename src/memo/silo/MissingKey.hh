#pragma once

#include <elle/Error.hh>

#include <memo/silo/Key.hh>

namespace memo
{
  namespace silo
  {
    class MissingKey
      : public elle::Error
    {
    public:
      using Super = elle::Error;
      MissingKey(Key key);
      MissingKey(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override;
      ELLE_ATTRIBUTE_R(Key, key);
    };
  }
}
