#include <infinit/RPC.hh>

#include <infinit/utility.hh>

namespace infinit
{
  RPCServer::RPCServer()
    : RPCServer(infinit::version())
  {}

  RPCServer::RPCServer(elle::Version version)
    : _version(version)
  {}
}
