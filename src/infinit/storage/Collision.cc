#include <infinit/storage/Collision.hh>

#include <elle/printf.hh>

namespace infinit
{
  namespace storage
  {
    Collision::Collision(Key key)
      : Super(elle::sprintf("collision on key: %s", key))
      , _key(key)
    {}
  }
}
