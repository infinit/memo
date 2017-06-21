#include <memo/RPC.hh>

#include <memo/utility.hh>

namespace memo
{
  RPCServer::RPCServer()
    : RPCServer(memo::version())
  {}

  RPCServer::RPCServer(elle::Version version)
    : _version(version)
  {}

  std::ostream&
  operator <<(std::ostream& output, RPCHandler const& rpc)
  {
    elle::fprintf(output, "RPC(%s)", rpc.name());
    return output;
  }

  namespace
  {
    auto const _register_serialization =
      elle::serialization::Hierarchy<elle::Exception>::Register<UnknownRPC>{};
  }
}
