#include <infinit/model/doughnut/protocol.hh>

namespace elle
{
  namespace serialization
  {
    using namespace infinit::model::doughnut;
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
        default:
          elle::unreachable();
      }
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
