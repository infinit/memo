#include <elle/test.hh>

#include <reactor/scheduler.hh>
#include <reactor/network/tcp-server.hh>
#include <reactor/network/tcp-socket.hh>

#include <protocol/Serializer.hh>
#include <protocol/ChanneledStream.hh>

#include <infinit/RPC.hh>
#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("RPC");

struct Server
{
  Server(std::function<void (infinit::RPCServer&)> rpcs)
    : server()
    , server_thread(
      new reactor::Thread("server", std::bind(&Server::serve, this)))
    , rpcs(rpcs)
  {
    server.listen();
  }

  void
  serve()
  {
    auto socket = server.accept();
    infinit::RPCServer s;
    if (this->rpcs)
      this->rpcs(s);
    s.serve(*socket);
  }

  reactor::network::TCPSocket
  connect()
  {
    return reactor::network::TCPSocket("127.0.0.1", this->server.port());
  }

  reactor::network::TCPServer server;
  reactor::Thread::unique_ptr server_thread;
  std::function<void (infinit::RPCServer&)> rpcs;
};

ELLE_TEST_SCHEDULED(move)
{
  Server s(
    [] (infinit::RPCServer& s)
    {
      s.add("coin",
            std::function<std::unique_ptr<int>(int, std::unique_ptr<int>)>(
              [] (int a, std::unique_ptr<int> b)
              { return elle::make_unique<int>(a + *b); }));
    });
  auto stream = s.connect();
  infinit::protocol::Serializer serializer(stream, infinit::version(), false);
  infinit::protocol::ChanneledStream channels(serializer);
  infinit::RPC<std::unique_ptr<int> (int, std::unique_ptr<int>)>
    rpc("coin", channels, infinit::version());
  try
  {
    BOOST_CHECK_EQUAL(*rpc(7, elle::make_unique<int>(35)), 42);
  }
  catch (std::exception const& e)
  {
    BOOST_FAIL(elle::sprintf("RPC exception: %s", e.what()));
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(move));
}
