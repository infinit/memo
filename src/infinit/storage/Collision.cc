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

    Collision::Collision(elle::serialization::SerializerIn& input)
      : Super(input)
    {
      input.serialize("key", _key);
    }

    void
    Collision::serialize(elle::serialization::Serializer& s,
                         elle::Version const& v)
    {
      Super::serialize(s, v);
      s.serialize("key", _key);
    }

    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<Collision> _register_serialization;
  }
}
