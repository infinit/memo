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
  Server(std::function<void (infinit::RPCServer&)> rpcs = {})
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
    s.add("succ", std::function<int (int)>([] (int x) { return x + 1; }));
    infinit::protocol::Serializer serializer(*socket, infinit::version(), false);
    infinit::protocol::ChanneledStream channels(serializer);
    this->channels = &channels;
    s.serve(channels);
    this->channels = nullptr;
  }

  reactor::network::TCPSocket
  connect()
  {
    return reactor::network::TCPSocket("127.0.0.1", this->server.port());
  }

  reactor::network::TCPServer server;
  reactor::Thread::unique_ptr server_thread;
  std::function<void (infinit::RPCServer&)> rpcs;
  infinit::protocol::ChanneledStream* channels;
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

ELLE_TEST_SCHEDULED(unknown)
{
  Server s;
  auto stream = s.connect();
  infinit::protocol::Serializer serializer(stream, infinit::version(), false);
  infinit::protocol::ChanneledStream channels(serializer);
  infinit::RPC<int (int)> unknown("unknown", channels, infinit::version());
  BOOST_CHECK_THROW(unknown(0), infinit::UnknownRPC);
  infinit::RPC<int (int)> succ("succ", channels, infinit::version());
  BOOST_CHECK_EQUAL(succ(0), 1);
}

ELLE_TEST_SCHEDULED(simultaneous)
{
  Server s(
    [] (infinit::RPCServer& s)
    {
      s.add("ping",
        std::function<int(int)>(
          [] (int a) {
            return a+1;
          }));
    });
  auto stream = s.connect();
  infinit::protocol::Serializer serializer(stream, infinit::version(), false);
  infinit::protocol::ChanneledStream channels(serializer);
  infinit::RPC<int (int)> ping("ping", channels, infinit::version());
  elle::With<reactor::Scope>() << [&](reactor::Scope& s)
  {
    for (int i=0; i<5; ++i)
      s.run_background("ping", [&] {
          BOOST_CHECK_EQUAL(ping(10), 11);
      });
    for (int i=0; i<5; ++i)
      s.run_background("ping", [&,i] {
          for (int y=0; y<i; ++y)
            reactor::yield();
          BOOST_CHECK_EQUAL(ping(10), 11);
      });
    reactor::wait(s);
  };
}

ELLE_TEST_SCHEDULED(bidirectional)
{
  Server s(
    [] (infinit::RPCServer& s)
    {
      s.add("ping",
        std::function<int(int)>(
          [] (int a) {
            for (int i=0; i<rand()%5; ++i)
              reactor::yield();
            return a+1;
          }));
    });
  auto stream = s.connect();
  infinit::protocol::Serializer serializer(stream, infinit::version(), false);
  infinit::protocol::ChanneledStream channels(serializer);
  infinit::RPCServer rev;
  rev.add("ping",
        std::function<int(int)>(
          [] (int a) {
            for (int i=0; i<rand()%5; ++i)
              reactor::yield();
            return a+2;
          }));
  auto t = elle::make_unique<reactor::Thread>("rev serve", [&] {
      rev.serve(channels);
  });
  infinit::RPC<int (int)> ping("ping", channels, infinit::version());
  BOOST_CHECK_EQUAL(ping(1), 2);
  infinit::RPC<int (int)> pingrev("ping", *s.channels, infinit::version());
  BOOST_CHECK_EQUAL(pingrev(1), 3);
  auto pinger = [](infinit::RPC<int (int)>& p, int delta) {
    for (int i=0; i<100; ++i)
      BOOST_CHECK_EQUAL(p(i), i+delta);
  };
  auto tfwd = elle::make_unique<reactor::Thread>("ping", [&] {
      pinger(ping, 1);
  });
  auto trev = elle::make_unique<reactor::Thread>("ping", [&] {
      pinger(pingrev, 2);
  });
  reactor::wait(*tfwd);
  reactor::wait(*trev);
  t->terminate_now();
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(move));
  suite.add(BOOST_TEST_CASE(unknown));
  suite.add(BOOST_TEST_CASE(bidirectional));
  suite.add(BOOST_TEST_CASE(simultaneous));
}
