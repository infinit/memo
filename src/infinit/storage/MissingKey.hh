#ifndef INFINIT_STORAGE_MISSING_KEY_HH
# define INFINIT_STORAGE_MISSING_KEY_HH

# include <elle/Error.hh>

# include <infinit/storage/Key.hh>

namespace infinit
{
  namespace storage
  {
    class MissingKey
      : public elle::Error
    {
    public:
      typedef elle::Error Super;
      MissingKey(Key key);
      MissingKey(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const& version) override;
      ELLE_ATTRIBUTE_R(Key, key);
    };
  }
}

#endif
