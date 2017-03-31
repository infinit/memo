#pragma once

#include <elle/Error.hh>

#include <infinit/storage/Key.hh>

namespace infinit
{
  namespace storage
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
