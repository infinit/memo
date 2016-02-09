#ifndef INFINIT_MODEL_MISSING_BLOCK_HH
# define INFINIT_MODEL_MISSING_BLOCK_HH

# include <elle/Error.hh>
# include <elle/attribute.hh>

# include <infinit/model/Address.hh>

namespace infinit
{
  namespace model
  {
    class MissingBlock
      : public elle::Error
    {
    public:
      typedef elle::Error Super;
      MissingBlock(Address address);
      MissingBlock(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s,
                elle::Version const&) override;
      ELLE_ATTRIBUTE_R(Address, address);
    };
  }
}

#endif
