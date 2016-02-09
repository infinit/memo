#include <infinit/model/MissingBlock.hh>

#include <elle/printf.hh>

namespace infinit
{
  namespace model
  {
    MissingBlock::MissingBlock(Address address)
      : Super(elle::sprintf("missing block: %x", address))
      , _address(address)
    {}

    MissingBlock::MissingBlock(elle::serialization::SerializerIn& input)
      : Super(input)
    {
      input.serialize("address", _address);
    }

    void
    MissingBlock::serialize(elle::serialization::Serializer& s,
                            elle::Version const& v)
    {
      Super::serialize(s, v);
      s.serialize("address", _address);
    }

    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<MissingBlock> _register_serialization;
  }
}
