#include <infinit/RPC.hh>

#include <infinit/version.hh>

namespace infinit
{
  RPCServer::RPCServer()
    : RPCServer(elle::Version(INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR))
  {}

  RPCServer::RPCServer(elle::Version version)
    : _version(version)
  {}
}
