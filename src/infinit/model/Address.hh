#ifndef INFINIT_MODEL_ADDRESS_HH
# define INFINIT_MODEL_ADDRESS_HH

# include <cstdint>
# include <cstring>
# include <functional>
# include <utility>

# include <elle/attribute.hh>

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
      bool
      operator ==(Address const& rhs) const;
      bool
      operator <(Address const& rhs) const;
      ELLE_ATTRIBUTE_R(Value, value);
      friend
      std::ostream&
      operator << (std::ostream& out, Address const& k);
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
