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
  }
}
