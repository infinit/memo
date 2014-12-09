#include <infinit/storage/MissingKey.hh>

#include <elle/printf.hh>

namespace infinit
{
  namespace storage
  {
    MissingKey::MissingKey(Key key)
      : Super(elle::sprintf("missing key: %s", key))
      , _key(key)
    {}
  }
}
