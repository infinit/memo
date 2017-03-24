#pragma once

#include <elle/serialization/Serializer.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /// Whether to support UTP and/or TCP.
      class Protocol
      {
      public:
        enum protocol
        {
          tcp = 1,
          utp = 2,
          all = 3
        };
        Protocol(protocol p = all)
          : _p{p}
        {}
        operator protocol() const { return _p; }

        bool with_all() const { return _p == all; }
        bool with_tcp() const { return _p == tcp || _p == all; }
        bool with_utp() const { return _p == utp || _p == all; }

      private:
        protocol _p;
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
