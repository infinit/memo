#include <memo/model/doughnut/protocol.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      using namespace elle::serialization;
      std::ostream&
      operator <<(std::ostream& out, Protocol protocol)
      {
        return out << Serialize<Protocol>::convert(protocol);
      }

      Protocol
      make_protocol(std::string const& name)
      {
        return elle::serialization::Serialize<Protocol>::convert(name);
      }
    }
  }
}

namespace elle
{
  namespace serialization
  {
    using namespace memo::model::doughnut;
    std::string
    Serialize<Protocol>::convert(Protocol p)
    {
      switch (p)
      {
      case Protocol::tcp:
        return "tcp";
      case Protocol::utp:
        return "utp";
      case Protocol::all:
        return "all";
      }
      elle::unreachable();
    }

    Protocol
    Serialize<Protocol>::convert(std::string const& repr)
    {
      if (repr == "tcp")
        return Protocol::tcp;
      else if (repr == "utp")
        return Protocol::utp;
      else if (repr == "all")
        return Protocol::all;
      else
        throw Error("Expected one of tcp, utp, all,  got '" + repr + "'");
    }
  }
}
