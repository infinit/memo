#include <infinit/overlay/Stonehenge.hh>

#include <iterator>

#include <elle/Error.hh>
#include <elle/assert.hh>

namespace infinit
{
  namespace overlay
  {
    /*-------------.
    | Construction |
    `-------------*/

    Stonehenge::Stonehenge(Members members)
      : _members(std::move(members))
    {
      if (this->_members.empty())
        throw elle::Error("empty peer list");
    }

    /*-------.
    | Lookup |
    `-------*/

    Stonehenge::Members
    Stonehenge::_lookup(model::Address address, int n, Operation) const
    {
      // Use modulo on the address to determine the owner and yield the n
      // following nodes.
      int size = this->_members.size();
      ELLE_ASSERT_LTE(n, size);
      auto owner = address.value()[0] % size;
      Stonehenge::Members res;
      auto write = std::back_inserter(res);
      auto start = this->_members.begin() + owner;
      auto end = std::min(start + n, this->_members.end());
      std::copy(start, end, write);
      if (owner + n > size)
      {
        auto start = this->_members.begin();
        auto end = start + n - (size - owner);
        std::copy(start, end, write);
      }
      return res;
    }
  }
}
