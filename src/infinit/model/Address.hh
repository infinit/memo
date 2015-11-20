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

    class Address
    {
    public:
      typedef uint8_t Value[32];
      Address();
      Address(Value bytes);
      Address(elle::UUID const& id);
      bool
      operator ==(Address const& rhs) const;
      bool
      operator !=(Address const& rhs) const;
      bool
      operator <(Address const& rhs) const;
      ELLE_ATTRIBUTE_R(Value, value);
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
