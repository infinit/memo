#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#include <boost/operators.hpp>

#include <elle/UUID.hh>
#include <elle/attribute.hh>
#include <elle/serialization/Serializer.hh>

namespace memo
{
  namespace model
  {
    namespace flags
    {
      static const uint8_t mutable_block = 0;
      static const uint8_t immutable_block = 1;
    }

    class Address
      : private boost::totally_ordered<Address>
    {
    public:
      using Self = Address;
      using Value = uint8_t[32];
      using Flags = uint8_t;
      static constexpr int flag_byte = 31;
      /// The null address.
      Address();
      Address(Value const value);
      Address(Value const value, Flags flags, bool combine);
      Address(elle::UUID const& id);
      /// Whether is not the null Address.
      operator bool() const;
      /// Ternary comparison, as with memcmp.
      int
      cmp(Address const& rhs) const;
      friend
      std::ostream&
      operator << (std::ostream& out, Address const& k);
      static
      Address
      from_string(std::string const& repr);
      static
      Address
      random();
      static
      Address
      random(Flags flags);
      /// A default initialized Address.
      static Address const null;
    private:
      friend
      struct elle::serialization::Serialize<memo::model::Address>;
      ELLE_ATTRIBUTE_R(Value, value);
      ELLE_ATTRIBUTE_R(bool, mutable_block);
    };

    bool
    operator ==(Address const& lhs, Address const& rhs);

    bool
    operator <(Address const& lhs, Address const& rhs);

    std::ostream&
    operator << (std::ostream& out, Address const& k);

    std::size_t
    hash_value(Address const& address);

    /// Compare excluding flag byte, for backward compatibility
    bool
    equal_unflagged(Address const& lhs, Address const& rhs);
  }
}

namespace std
{
  template <>
  struct hash<memo::model::Address>
  {
    size_t
    operator()(memo::model::Address const& employee) const;
  };
}

namespace elle
{
  namespace serialization
  {
    template <>
    struct Serialize<memo::model::Address>
    {
      using Type = elle::Buffer;
      static
      Type
      convert(memo::model::Address& address);
      static
      memo::model::Address
      convert(Type repr);
    };
  }
}
