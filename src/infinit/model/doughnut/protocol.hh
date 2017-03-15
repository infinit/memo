#pragma once

#include <elle/serialization/Serializer.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      enum class Protocol
      {
        tcp = 1,
        utp = 2,
        all = 3
      };

      std::ostream&
      operator <<(std::ostream&, Protocol);

      Protocol
      make_protocol(std::string const& name);
    }
  }
}

namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<infinit::model::doughnut::Protocol>
    {
      using Type = std::string;
      static
      std::string
      convert(infinit::model::doughnut::Protocol p);
      static
      infinit::model::doughnut::Protocol
      convert(std::string const& repr);
    };
  }
}
