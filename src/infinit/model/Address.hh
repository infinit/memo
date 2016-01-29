#ifndef INFINIT_MODEL_ADDRESS_HH
# define INFINIT_MODEL_ADDRESS_HH

# include <cstdint>
# include <cstring>
# include <functional>
# include <utility>

# include <elle/UUID.hh>
# include <elle/attribute.hh>
# include <elle/serialization/Serializer.hh>

namespace infinit
{
  namespace model
  {
    using namespace std::rel_ops;

    namespace flags
    {
      static const uint8_t block_kind = 1;
      static const uint8_t mutable_block = 0;
      static const uint8_t immutable_block = 1;
    }

    class Address
    {
    public:
      typedef uint8_t Value[32];
      typedef uint8_t Flags;
      static const int flag_byte = 31;
      Address();
      Address(Value const bytes);
      Address(Value const bytes, Flags flags);
      Address(elle::UUID const& id);
      bool
      operator ==(Address const& rhs) const;
      bool
      operator !=(Address const& rhs) const;
      bool
      operator <(Address const& rhs) const;
      ELLE_ATTRIBUTE_R(Value, value);
      ELLE_ATTRIBUTE_R(uint8_t, overwritten_value);
      Address unflagged() const; ///< Return initial address not masked by flags
      Flags  flags() const;
      bool mutable_block() const; ///< True if flags contain mutable_block
      friend
      std::ostream&
      operator << (std::ostream& out, Address const& k);
      static
      Address
      from_string(std::string const& repr);
      static
      Address
      random();
      static Address const null;
    private:
      friend
      struct elle::serialization::Serialize<infinit::model::Address>;
    };

    std::ostream&
    operator << (std::ostream& out, Address const& k);

    std::size_t
    hash_value(Address const& address);
  }
}

namespace std
{
  template <>
  struct hash<infinit::model::Address>
  {
    size_t
    operator()(infinit::model::Address const& employee) const;
  };
}

namespace elle
{
  namespace serialization
  {
    template <>
    struct Serialize<infinit::model::Address>
    {
      typedef elle::Buffer Type;
      static
      Type
      convert(infinit::model::Address& address);
      static
      infinit::model::Address
      convert(Type repr);
    };
  }
}

#endif
