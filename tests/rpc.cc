#include <elle/test.hh>

#include <reactor/scheduler.hh>
#include <reactor/network/tcp-server.hh>
#include <reactor/network/tcp-socket.hh>

#include <protocol/Serializer.hh>
#include <protocol/ChanneledStream.hh>

#include <infinit/version.hh>
#include <infinit/RPC.hh>

ELLE_LOG_COMPONENT("RPC");

ELLE_TEST_SCHEDULED(move)
{
  reactor::network::TCPServer server;
  server.listen();

  reactor::Thread::unique_ptr server_thread(new reactor::Thread(
    "server",
    [&]
    {
      auto socket = server.accept();
      infinit::RPCServer s;
      s.add("coin",
            std::function<std::unique_ptr<int>(int, std::unique_ptr<int>)>(
              [] (int a, std::unique_ptr<int> b)
              { return elle::make_unique<int>(a + *b); }));
      s.serve(*socket);
    }));
  elle::Version version(INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR);
  reactor::network::TCPSocket stream("127.0.0.1", server.port());
  infinit::protocol::Serializer serializer(stream, version, false);
  infinit::protocol::ChanneledStream channels(serializer);
  infinit::RPC<std::unique_ptr<int> (int, std::unique_ptr<int>)>
    rpc("coin", channels, version);
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
