#ifndef INFINIT_STORAGE_INSUFFICIENT_SPACE_HH
# define INFINIT_STORAGE_INSUFFICIENT_SPACE_HH

# include <elle/Error.hh>
# include <elle/serialization/Serializer.hh>
# include <elle/serialization/SerializerIn.hh>

namespace infinit
{
  namespace storage
  {
    class InsufficientSpace
      : public elle::Error
    {
    public:
      using Super = elle::Error;
      InsufficientSpace(int delta, int64_t usage, int64_t capacity);
      InsufficientSpace(elle::serialization::SerializerIn& in);
      virtual
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const&) override;

      ELLE_ATTRIBUTE_R(int, delta);
      ELLE_ATTRIBUTE_R(int64_t, usage);
      ELLE_ATTRIBUTE_R(int64_t, capacity);
    };
  }
}

#endif
