#pragma once

#include <elle/Error.hh>

#include <memo/silo/Key.hh>

namespace memo
{
  namespace silo
  {
    class Collision
      : public elle::Error
    {
    public:
      using Super = elle::Error;
      Collision(Key key);
      Collision(elle::serialization::SerializerIn& s);
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& v) override;
      ELLE_ATTRIBUTE_R(Key, key);
    };
  }
}
