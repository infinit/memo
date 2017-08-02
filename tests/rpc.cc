#include <elle/test.hh>

#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Scope.hh>
#include <elle/reactor/network/tcp-server.hh>
#include <elle/reactor/network/tcp-socket.hh>

#include <elle/protocol/Serializer.hh>
#include <elle/protocol/ChanneledStream.hh>

#include <memo/RPC.hh>
#include <memo/utility.hh>

ELLE_LOG_COMPONENT("test.rpc");

struct Server
{
  Server(std::function<void (memo::RPCServer&)> rpcs = {})
    : server()
    , server_thread(
      new elle::reactor::Thread("server", std::bind(&Server::serve, this)))
    , rpcs(rpcs)
  {
    server.listen();
  }

  void
  serve()
  {
    auto socket = server.accept();
    memo::RPCServer s;
    if (this->rpcs)
      this->rpcs(s);
    s.add("succ", [] (int x) { return x + 1; });
    elle::protocol::Serializer serializer(*socket, memo::version(), false);
    auto&& channels = elle::protocol::ChanneledStream{serializer};
    this->channels = &channels;
    s.serve(channels);
    this->channels = nullptr;
  }

  elle::reactor::network::TCPSocket
  connect()
  {
    return elle::reactor::network::TCPSocket("127.0.0.1", this->server.port());
  }

  elle::reactor::network::TCPServer server;
  elle::reactor::Thread::unique_ptr server_thread;
  std::function<void (memo::RPCServer&)> rpcs;
  elle::protocol::ChanneledStream* channels;
};

ELLE_TEST_SCHEDULED(move)
{
  Server s(
    [] (memo::RPCServer& s)
    {
      s.add("coin",
            [] (int a, std::unique_ptr<int> b)
            { return std::make_unique<int>(a + *b); });
    });
  auto stream = s.connect();
  elle::protocol::Serializer serializer(stream, memo::version(), false);
  auto&& channels = elle::protocol::ChanneledStream{serializer};
  memo::RPC<std::unique_ptr<int> (int, std::unique_ptr<int>)>
    rpc("coin", channels, memo::version());
  try
  {
    BOOST_CHECK_EQUAL(*rpc(7, std::make_unique<int>(35)), 42);
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
  elle::protocol::Serializer serializer(stream, memo::version(), false);
  auto&& channels = elle::protocol::ChanneledStream{serializer};
  memo::RPC<int (int)> unknown("unknown", channels, memo::version());
  BOOST_CHECK_THROW(unknown(0), memo::UnknownRPC);
  memo::RPC<int (int)> succ("succ", channels, memo::version());
  BOOST_CHECK_EQUAL(succ(0), 1);
}

ELLE_TEST_SCHEDULED(simultaneous)
{
  Server s(
    [] (memo::RPCServer& s)
    {
      s.add("ping", [] (int a) {return a+1;});
    });
  auto stream = s.connect();
  elle::protocol::Serializer serializer(stream, memo::version(), false);
  auto&& channels = elle::protocol::ChanneledStream{serializer};
  memo::RPC<int (int)> ping("ping", channels, memo::version());
  elle::With<elle::reactor::Scope>() << [&](elle::reactor::Scope& s)
  {
    for (int i=0; i<5; ++i)
      s.run_background("ping", [&] {
          BOOST_CHECK_EQUAL(ping(10), 11);
      });
    for (int i=0; i<5; ++i)
      s.run_background("ping", [&,i] {
          for (int y=0; y<i; ++y)
            elle::reactor::yield();
          BOOST_CHECK_EQUAL(ping(10), 11);
      });
    elle::reactor::wait(s);
  };
}

ELLE_TEST_SCHEDULED(bidirectional)
{
  Server s(
    [] (memo::RPCServer& s)
    {
      s.add("ping",
            [] (int a) {
              for (int i=0; i<rand()%5; ++i)
                elle::reactor::yield();
              return a+1;
            });
    });
  auto stream = s.connect();
  elle::protocol::Serializer serializer(stream, memo::version(), false);
  auto&& channels = elle::protocol::ChanneledStream{serializer};
  memo::RPCServer rev;
  rev.add("ping",
          [] (int a) {
            for (int i=0; i<rand()%5; ++i)
              elle::reactor::yield();
            return a+2;
          });
  auto t = std::make_unique<elle::reactor::Thread>("rev serve", [&] {
      rev.serve(channels);
  });
  memo::RPC<int (int)> ping("ping", channels, memo::version());
  BOOST_CHECK_EQUAL(ping(1), 2);
  memo::RPC<int (int)> pingrev("ping", *s.channels, memo::version());
  BOOST_CHECK_EQUAL(pingrev(1), 3);
  auto pinger = [](memo::RPC<int (int)>& p, int delta) {
    for (int i=0; i<100; ++i)
      BOOST_CHECK_EQUAL(p(i), i+delta);
  };
  auto tfwd = std::make_unique<elle::reactor::Thread>("ping", [&] {
      pinger(ping, 1);
  });
  auto trev = std::make_unique<elle::reactor::Thread>("ping", [&] {
      pinger(pingrev, 2);
  });
  elle::reactor::wait(*tfwd);
  elle::reactor::wait(*trev);
  t->terminate_now();
}

ELLE_TEST_SCHEDULED(parallel)
{
  auto const delay_ms = valgrind(120, 4);
  auto const delay = std::chrono::milliseconds(delay_ms);
  {
    memo::setenv("RPC_SERVE_THREADS", 5);
    Server s(
      [&] (memo::RPCServer& s)
      {
        s.add("ping", [&] (int a) {
            elle::reactor::sleep(1_ms * delay_ms);
            return a+1;
        });
      });
    auto stream = s.connect();
    elle::protocol::Serializer serializer(stream, memo::version(), false);
    auto&& channels = elle::protocol::ChanneledStream{serializer};
    memo::RPC<int (int)> ping("ping", channels, memo::version());
    auto start = std::chrono::system_clock::now();
    elle::With<elle::reactor::Scope>() << [&](elle::reactor::Scope& s)
    {
      for (int i=0; i<10; ++i)
        s.run_background("ping", [&] {
            BOOST_TEST(ping(10) == 11);
        });
      elle::reactor::wait(s);
    };
    auto duration = std::chrono::system_clock::now() - start;
    BOOST_TEST_MESSAGE("delay: " << delay << ", duration: " << duration);
    BOOST_TEST(delay * 2 <= duration);
    BOOST_TEST(duration <= delay * 3);
  }
  {
    memo::setenv("RPC_SERVE_THREADS", 0);
    Server s(
      [&] (memo::RPCServer& s)
      {
        s.add("ping", [&] (int a) {
            elle::reactor::sleep(1_ms * delay_ms);
            return a+1;
        });
      });
    auto stream = s.connect();
    elle::protocol::Serializer serializer(stream, memo::version(), false);
    auto&& channels = elle::protocol::ChanneledStream{serializer};
    memo::RPC<int (int)> ping("ping", channels, memo::version());
    auto start = std::chrono::system_clock::now();
    elle::With<elle::reactor::Scope>() << [&](elle::reactor::Scope& s)
    {
      for (int i=0; i<10; ++i)
        s.run_background("ping", [&] {
            BOOST_CHECK_EQUAL(ping(10), 11);
        });
      elle::reactor::wait(s);
    };
    auto duration = std::chrono::system_clock::now() - start;
    BOOST_TEST(delay * 1 <= duration);
    BOOST_TEST(duration <= delay * 2);
  }
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(move));
  suite.add(BOOST_TEST_CASE(unknown));
  suite.add(BOOST_TEST_CASE(bidirectional));
  suite.add(BOOST_TEST_CASE(simultaneous));
  suite.add(BOOST_TEST_CASE(parallel));
}
