#include <infinit/model/Address.hh>

#include <elle/Buffer.hh>

namespace infinit
{
  namespace model
  {
    Address::Address(Value value)
      : _value()
    {
      ::memcpy(this->_value, value, sizeof(Value));
    }

    bool
    Address::operator ==(Address const& rhs) const
    {
      return memcmp(this->_value, rhs._value, sizeof(Value)) == 0;
    }

    bool
    Address::operator <(Address const& rhs) const
    {
      return memcmp(this->_value, rhs._value, sizeof(Value)) < 0;
    }

    std::ostream&
    operator << (std::ostream& out, Address const& k)
    {
      out << elle::ConstWeakBuffer(k._value, sizeof(k._value));
      return out;
    }

    Address::Address(elle::serialization::SerializerIn& s)
      : _value()
    {
      this->serialize(s);
    }

    void
    Address::serialize(elle::serialization::Serializer& s)
    {
      elle::WeakBuffer buf(this->_value, sizeof(Value));
      s.serialize("id", buf);
    }

    Address
    Address::from_string(std::string const& repr)
    {
      if (repr.length() != 64)
        throw elle::Error(elle::sprintf("invalid address: %s", repr));
      Value v;
      char c[3] = {0, 0, 0};
      for (int i = 0; i < 32; ++i)
      {
        c[0] = repr[i * 2];
        c[1] = repr[i * 2 + 1];
        v[i] = strtol(c, nullptr, 16);
      }
      return Address(v);
    }
  }
}

namespace std
{
  // Shamelessly stolen from Boost, the internet and the universe.
  inline
  void
  hash_combine(std::size_t& seed, std::size_t value)
  {
    seed ^= value + 0x9e3779b9 + (seed<<6) + (seed>>2);
  }

  size_t
  std::hash<infinit::model::Address>::operator()(
    infinit::model::Address const& address) const
  {
    std::size_t result = 0;
    for (unsigned int i = 0;
         i < sizeof(infinit::model::Address::Value);
         i += sizeof(std::size_t))
      hash_combine(result,
                   *reinterpret_cast<std::size_t const*>(address.value() + i));
    return result;
  }
}
