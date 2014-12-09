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
      ELLE_ATTRIBUTE_R(Key, key);
    };
  }
}

#endif
