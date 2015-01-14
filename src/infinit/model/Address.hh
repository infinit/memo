#ifndef INFINIT_MODEL_ADDRESS_HH
# define INFINIT_MODEL_ADDRESS_HH

# include <cstdint>
# include <cstring>
# include <functional>
# include <utility>

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
      Address(Value bytes);
      Address(elle::serialization::SerializerIn& s);
      bool
      operator ==(Address const& rhs) const;
      bool
      operator <(Address const& rhs) const;
      void
      serialize(elle::serialization::Serializer& s);
      ELLE_ATTRIBUTE_R(Value, value);
      friend
      std::ostream&
      operator << (std::ostream& out, Address const& k);
      static
      Address
      from_string(std::string const& repr);
    };

    std::ostream&
    operator << (std::ostream& out, Address const& k);
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

#endif
