#include <infinit/model/Address.hh>

#include <boost/uuid/random_generator.hpp>

#include <elle/Buffer.hh>

#include <cryptography/hash.hh>

namespace infinit
{
  namespace model
  {
    Address::Address()
      : _value()
    {
      memset(this->_value, 0, sizeof(Address::Value));
    }

    Address::Address(Value value)
      : _value()
    {
      ::memcpy(this->_value, value, sizeof(Value));
    }

    Address::Address(elle::UUID const& id)
      : Address(infinit::cryptography::hash(
                  elle::ConstWeakBuffer(id.data, id.static_size()),
                  infinit::cryptography::Oneway::sha256).contents())
    {}

    bool
    Address::operator ==(Address const& rhs) const
    {
      return memcmp(this->_value, rhs._value, sizeof(Value)) == 0;
    }

    bool
    Address::operator !=(Address const& rhs) const
    {
      return memcmp(this->_value, rhs._value, sizeof(Value)) != 0;
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

    static
    boost::mt19937
    random_generator()
    {
      boost::mt19937 ran;
      return ran;
    }

    Address
    Address::random()
    {
      // Hash a UUID to get a random address.  Like using a deathstar to blow
      // a mosquito and I like it.
      static auto random = random_generator();
      static boost::uuids::basic_random_generator<boost::mt19937> g(&random);
      auto id = g();
      auto hash = cryptography::hash(
        elle::ConstWeakBuffer(id.data, id.static_size()),
        cryptography::Oneway::sha256);
      ELLE_ASSERT_GTE(hash.size(), sizeof(Address::Value));
      return Address(hash.contents());
    }

    Address const Address::null;

    std::size_t
    hash_value(Address const& address)
    {
      return std::hash<Address>()(address);
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

namespace elle
{
  namespace serialization
  {
    using infinit::model::Address;
    Serialize<Address>::Type
    Serialize<Address>::convert(Address& address)
    {
      return Type(address._value, sizeof(Address::Value));
    }

    Address
    Serialize<Address>::convert(Type buffer)
    {
      if (buffer.size() == 0)
        return Address();
      if (buffer.size() != sizeof(Address::Value))
        throw elle::Error(elle::sprintf("invalid address: %x", buffer));
      Address::Value value;
      memcpy(value, buffer.contents(), sizeof(value));
      return Address(value);
    }
  }
}
