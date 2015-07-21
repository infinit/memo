#ifndef INFINIT_STORAGE_COLLISION_HH
# define INFINIT_STORAGE_COLLISION_HH

# include <elle/Error.hh>

# include <infinit/storage/Key.hh>

namespace infinit
{
  namespace storage
  {
    class Collision
      : public elle::Error
    {
    public:
      typedef elle::Error Super;
      Collision(Key key);
      Collision(elle::serialization::SerializerIn& s);
      void serialize(elle::serialization::Serializer& s);
      ELLE_ATTRIBUTE_R(Key, key);
    };
  }
}

#endif
