#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/algorithm/cxx11/none_of.hpp>
#include <boost/range/algorithm/count_if.hpp>
#include <boost/range/algorithm/find_if.hpp>

#include <elle/test.hh>

#include <elle/das/printer.hh>
#include <elle/das/serializer.hh>
#include <elle/das/Symbol.hh>
#include <elle/err.hh>
#include <elle/make-vector.hh>

#include <elle/reactor/network/udp-socket.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/overlay/kouncil/Kouncil.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.overlay.test");

#include "DHT.hh"

using namespace infinit::model;
using namespace infinit::model::blocks;
using namespace infinit::model::doughnut;
using namespace infinit::overlay;

using boost::algorithm::any_of_equal;
using boost::algorithm::none_of_equal;

/// Check that Element ∈ Set.
#define CHECK_IN(Element, Set)                  \
  BOOST_CHECK(any_of_equal(Set, Element))

/// Check that Element ∉ Set.
#define CHECK_NOT_IN(Element, Set)              \
  BOOST_CHECK(none_of_equal(Set, Element))



class TestConflictResolver
  : public DummyConflictResolver
{};

inline std::unique_ptr<ConflictResolver>
tcr()
{
  return std::make_unique<TestConflictResolver>();
}

class UTPInstrument
{
public:
  UTPInstrument(int port)
    : server()
    , _endpoint(boost::asio::ip::address::from_string("127.0.0.1"), port)
    , _thread(new elle::reactor::Thread(elle::sprintf("%s server", this),
                                        [this] { this->_serve(); }))
  {
    this->server.bind({});
    this->_transmission.open();
  }

  elle::reactor::network::UDPSocket server;
  ELLE_ATTRIBUTE_RX(elle::reactor::Barrier, transmission);

  Endpoint
  endpoint()
  {
    return Endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                    this->server.local_endpoint().port());
  }

private:
  ELLE_LOG_COMPONENT("infinit.overlay.test.UTPInstrument");

  ELLE_ATTRIBUTE(Endpoint, endpoint);
  ELLE_ATTRIBUTE(Endpoint, client_endpoint);
  void
  _serve()
  {
    char buf[10000];
    while (true)
    {
      try
      {
        elle::reactor::network::UDPSocket::EndPoint ep;
        auto sz = server.receive_from(elle::WeakBuffer(buf), ep);
        elle::reactor::wait(this->_transmission);
        if (ep.port() != _endpoint.port())
        {
          _client_endpoint = ep;
          server.send_to(elle::ConstWeakBuffer(buf, sz), _endpoint.udp());
        }
        else
          server.send_to(elle::ConstWeakBuffer(buf, sz), _client_endpoint.udp());
      }
      catch (elle::reactor::network::Error const& e)
      {
        ELLE_LOG("ignoring exception %s", e);
      }
    }
  }

  ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr, thread);
};

/// A TCP socket that we can close/reopen, for tests.
class TCPInstrument
{
public:
  TCPInstrument(int port)
    : server()
    , socket("127.0.0.1", port)
    , _endpoint(boost::asio::ip::address::from_string("127.0.0.1"), port)
    , _thread(new elle::reactor::Thread(elle::sprintf("%s server", this),
                                        [this] { this->_serve(); }))
  {
    this->server.listen();
    this->_transmission.open();
  }

  elle::reactor::network::TCPServer server;
  elle::reactor::network::TCPSocket socket;
  ELLE_ATTRIBUTE_RX(elle::reactor::Barrier, transmission);

  Endpoint
  endpoint()
  {
    return Endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
                    this->server.local_endpoint().port());
  }

  void
  close()
  {
    this->_thread->terminate_now();
    this->socket.close();
  }

private:
  ELLE_LOG_COMPONENT("infinit.overlay.test.TCPInstrument");

  ELLE_ATTRIBUTE(Endpoint, endpoint);
  ELLE_ATTRIBUTE(Endpoint, client_endpoint);

  void
  _forward(elle::reactor::network::TCPSocket& in,
           elle::reactor::network::TCPSocket& out)
  {
    char buf[10000];
    while (true)
    {
      try
      {
        auto size = in.read_some(elle::WeakBuffer(buf));
        elle::reactor::wait(this->_transmission);
        out.write(elle::WeakBuffer(buf, size));
      }
      catch (elle::reactor::network::ConnectionClosed const&)
      {
        break;
      }
      catch (elle::reactor::network::Error const& e)
      {
        ELLE_LOG("ignoring exception %s", e);
      }
    }
  }

  void
  _serve()
  {
    auto socket = this->server.accept();
    elle::With<elle::reactor::Scope>() << [&] (elle::reactor::Scope& scope)
    {
      scope.run_background(elle::sprintf("%s forward", this),
                           [this, &socket]
                           {
                             this->_forward(*socket, this->socket);
                           });
      scope.run_background(elle::sprintf("%s backward", this),
                           [this, &socket]
                           {
                             this->_forward(this->socket, *socket);
                           });
      elle::reactor::wait(scope);
    };
  }

  ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr, thread);
};


namespace
{
  /// Get n-th member of a container.
  template <typename C>
  auto
  get_n(C& c, int n)
  {
    return *std::next(c.begin(), n);
  }

  /// Get random member of a container.
  template <typename C>
  auto
  get_random(C& c)
  {
    return get_n(c, rand() % c.size());
  }

  elle::json::Object
  get_ostats(DHT& client)
  {
    auto stats = client.dht->overlay()->query("stats", {});
    return boost::any_cast<elle::json::Object>(stats);
  }

  /// @param ostats  A JSON stat object.
  /// @param type    "contacts", or "peers", etc.
  ///
  /// @throw std::out_of_range when the type is not in ostats.
  std::vector<infinit::model::Address>
  get_addresses(elle::json::Object const& ostats,
                std::string const& type)
  {
    ELLE_DUMP("%s", elle::json::pretty_print(ostats.at(type)));
    auto cts = boost::any_cast<elle::json::Array>(ostats.at(type));
    return elle::make_vector(cts, [](auto& c) {
      auto const& o = boost::any_cast<elle::json::Object>(c);
      return infinit::model::Address::from_string(
        boost::any_cast<std::string>(o.at("id")));
      });
  }

  std::vector<infinit::model::Address>
  get_peers(DHT& client, std::string const& field = {})
  {
    return get_addresses(get_ostats(client),
                         !field.empty() ? field
                         // In Kelips.
                         : get_kelips(client) ? "contacts"
                         // In Kouncil.
                         : "peers");
  }

  int
  peer_count(DHT& client, bool discovered = false)
  {
    int res = -1;
    auto const ostats = get_ostats(client);
    if (get_kelips(client))
    {
      auto cts = boost::any_cast<elle::json::Array>(ostats.at("contacts"));
      ELLE_DEBUG("%s", elle::json::pretty_print(cts));
      ELLE_TRACE("checking %s candidates", cts.size());
      res = boost::count_if(cts, [&](auto& c) {
          auto const& o = boost::any_cast<elle::json::Object>(c);
          return (!discovered
                  || boost::any_cast<bool>(o.at("discovered")));
        });
    }
    else
    {
      assert(get_kouncil(client));
      res = boost::any_cast<elle::json::Array>(ostats.at("peers")).size();
    }
    ELLE_TRACE("counted %s peers for %s", res, client.dht);
    return res;
  }

  /// Close the connection to this peer.
  template <typename Peer>
  void
  close_connection(Peer& p)
  {
    namespace net = elle::reactor::network;
    if (auto* s = dynamic_cast<net::TCPSocket*>(p.stream().get()))
      s->close();
    else if (auto* s = dynamic_cast<net::UTPSocket*>(p.stream().get()))
      s->close();
    else
      BOOST_FAIL(elle::sprintf("could not obtain socket pointer for %s", p));
  }


  void
  kouncil_wait_pasv(DHT& s, int n_servers)
  {
    // Get the addresses of the *connected* peers.
    auto get_addresses = [](elle::json::Array const& cts)
      {
        auto res = std::vector<infinit::model::Address>{};
        for (auto const& c: cts)
        {
          auto const& o = boost::any_cast<elle::json::Object>(c);
          if (boost::any_cast<bool> (o.at("connected")))
            res.emplace_back(infinit::model::Address::from_string
                             (boost::any_cast<std::string>(o.at("id"))));
        }
        return res;
      };

    while (true)
    {
      auto const ostats = get_ostats(s);
      ELLE_DEBUG("%s", elle::json::pretty_print(ostats.at("peers")));
      auto cts = boost::any_cast<elle::json::Array>(ostats.at("peers"));
      auto servers = get_addresses(cts);
      if (n_servers <= int(servers.size()))
        return;
      ELLE_TRACE("%s/%s", servers.size(), n_servers);
      elle::reactor::sleep(50_ms);
    }
  }

  // Wait until s sees n_servers and can make RPC calls to all of them
  // If or_more is true, accept extra non-working peers
  void
  hard_wait(DHT& s, int n_servers,
            infinit::model::Address client = {},
            bool or_more = false,
            infinit::model::Address blacklist = {})
  {
    int attempts = 0;
    while (true)
    {
      if (++attempts > 50 && !(attempts % 40))
        ELLE_LOG("hard_wait, attempt %s: %s",
                 attempts, elle::json::pretty_print(get_ostats(s)));
      bool ok = true;
      auto peers = get_peers(s);
      int hit = 0;
      if (peers.size() >= unsigned(n_servers))
        for (auto const& pa: peers)
          if (pa != client && pa != blacklist)
            try
            {
              auto p = s.dht->overlay()->lookup_node(pa);
              p.lock()->fetch(infinit::model::Address::random(), boost::none);
            }
            catch (infinit::storage::MissingKey const& mb)
            { // FIXME why do we need this?
              ++hit;
            }
            catch (infinit::model::MissingBlock const& mb)
            {
              ++hit;
            }
            catch (elle::Error const& e)
            {
              ELLE_TRACE("hard_wait %f: %s", pa, e);
              if (!or_more)
                ok = false;
            }
      if ((hit == n_servers || (or_more && hit >n_servers))
          && ok)
        break;
      elle::reactor::sleep(50_ms);
    }
    ELLE_DEBUG("hard_wait exiting");
  }

  struct TestConfiguration
  {
    Doughnut::OverlayBuilder overlay_builder;
    boost::optional<elle::Version> version;
  };
}

ELLE_TEST_SCHEDULED(
  basics, (TestConfiguration, config), (bool, anonymous))
{
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto storage = infinit::storage::Memory::Blocks();
  auto id = infinit::model::Address::random();
  auto make_dht_a = [&]
    {
      return DHT(
        ::version = config.version,
        ::id = id,
        ::keys = keys,
        ::storage = std::make_unique<infinit::storage::Memory>(storage),
        ::make_overlay = config.overlay_builder);
    };
  auto disk = [&]
    {
      ELLE_LOG_SCOPE("store first block on disk and restart first DHT");
      auto dht = make_dht_a();
      auto b = dht.dht->make_block<MutableBlock>(std::string("disk"));
      dht.dht->seal_and_insert(*b, tcr());
      return b;
    }();
  auto dht_a = make_dht_a();
  auto before = dht_a.dht->make_block<MutableBlock>(std::string("before"));
  ELLE_LOG("store second block in memory")
    dht_a.dht->seal_and_insert(*before, tcr());
  ELLE_LOG("connect second DHT");
  auto dht_b = DHT(::version = config.version,
                   ::keys = keys,
                   ::make_overlay = config.overlay_builder,
                   ::storage = nullptr);
  discover(dht_b, dht_a, anonymous, false, true);
  auto after = dht_a.dht->make_block<MutableBlock>(std::string("after"));
  ELLE_LOG("store third block")
    dht_a.dht->seal_and_insert(*after, tcr());
  ELLE_LOG("check non-existent block")
    BOOST_CHECK_THROW(
      dht_b.dht->overlay()->lookup(Address::random()),
      MissingBlock);
  ELLE_LOG("check first block - loaded from disk")
    BOOST_CHECK_EQUAL(
      dht_b.dht->overlay()->lookup(disk->address()).lock()->id(),
      dht_a.dht->id());
  ELLE_LOG("check second block - present before connection")
    BOOST_CHECK_EQUAL(
      dht_b.dht->overlay()->lookup(before->address()).lock()->id(),
      dht_a.dht->id());
  ELLE_LOG("check third block - inserted after connection")
    BOOST_CHECK_EQUAL(
      dht_b.dht->overlay()->lookup(after->address()).lock()->id(),
      dht_a.dht->id());
}

ELLE_TEST_SCHEDULED(
  dead_peer, (TestConfiguration, config), (bool, anonymous))
{
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto dht_a = DHT(::id = special_id(10),
                   ::version = config.version,
                   ::keys = keys,
                   ::make_overlay = config.overlay_builder,
                   ::paxos = false);
  elle::With<UTPInstrument>(dht_a.dht->local()->server_endpoints().begin()->port()) <<
    [&] (UTPInstrument& instrument)
    {
      auto dht_b = DHT(::id = special_id(11),
                       ::version = config.version,
                       ::keys = keys,
                       ::make_overlay = config.overlay_builder,
                       ::paxos = false,
                       ::storage = nullptr);
      auto loc = NodeLocation{anonymous ? Address::null : dht_a.dht->id(),
                              {instrument.endpoint()}};
      ELLE_LOG("connect DHTs")
        discover(dht_b, dht_a, loc, true);
      // Ensure one request can go through.
      {
        auto block = dht_a.dht->make_block<MutableBlock>(std::string("block"));
        ELLE_LOG("store block")
          dht_a.dht->seal_and_insert(*block, tcr());
        ELLE_LOG("lookup block")
        {
          auto remote = dht_b.dht->overlay()->lookup(block->address()).lock();
          BOOST_CHECK_EQUAL(remote->id(), dht_a.dht->id());
        }
      }
      // Partition peer
      ELLE_LOG("close transmission")
        instrument.transmission().close();
      // Ensure we don't deadlock
      {
        auto block = dht_a.dht->make_block<MutableBlock>(std::string("block"));
        ELLE_LOG("store block")
          dht_a.dht->seal_and_insert(*block, tcr());
      }
      ELLE_LOG("done");
    };
}

ELLE_TEST_SCHEDULED(
  discover_endpoints, (TestConfiguration, config), (bool, anonymous))
{
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto id_a = infinit::model::Address::random();
  auto dht_a = std::make_unique<DHT>(
    ::id = id_a,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::paxos = false);
  Address old_address;
  ELLE_LOG("store first block")
  {
    auto block = dht_a->dht->make_block<MutableBlock>(std::string("block"));
    dht_a->dht->seal_and_insert(*block, tcr());
    old_address = block->address();
  }
  auto dht_b = DHT(
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::storage = nullptr);
  discover(dht_b, *dht_a, anonymous, false, true);
  ELLE_LOG("lookup block")
  {
    BOOST_TEST(
      dht_b.dht->overlay()->lookup(old_address).lock()->id()
      == id_a);
  }
  auto disappeared = elle::reactor::waiter(
    dht_b.dht->overlay()->on_disappearance(),
    [&] (Address id, bool)
    {
      BOOST_CHECK_EQUAL(id, id_a);
      return true;
    });
  ELLE_LOG("restart first DHT")
  {
    dht_a.reset();
    dht_a = std::make_unique<DHT>(
      ::id = id_a,
      ::version = config.version,
      ::keys = keys,
      ::make_overlay = config.overlay_builder,
      ::paxos = false);
  }
  Address new_address;
  ELLE_LOG("store second block")
  {
    auto block = dht_a->dht->make_block<MutableBlock>(std::string("nblock"));
    dht_a->dht->seal_and_insert(*block, tcr());
    new_address = block->address();
  }
  ELLE_LOG("lookup second block")
    BOOST_CHECK_THROW(dht_b.dht->overlay()->lookup(new_address), MissingBlock);
  // If the peer does not disappear first, we can't wait for on_discovery.
  elle::reactor::wait(disappeared);
  ELLE_LOG("discover new endpoints")
    discover(dht_b, *dht_a, anonymous, false, true);
  ELLE_LOG("lookup second block")
  {
    BOOST_CHECK_EQUAL(
      dht_b.dht->overlay()->lookup(new_address).lock()->id(),
      id_a);
  }
}

ELLE_TEST_SCHEDULED(reciprocate,
                    (TestConfiguration, config), (bool, anonymous))
{
  infinit::storage::Memory::Blocks b1, b2;
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  ELLE_LOG("create DHTs");
  auto dht_a = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    ::paxos = true,
    dht::consensus::rebalance_auto_expand = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b1));
  auto dht_b = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false,
    ::paxos = true,
    ::storage = std::make_unique<infinit::storage::Memory>(b2));
  ELLE_LOG("connect DHTs")
    discover(*dht_b, *dht_a, anonymous, false, true, true);
  {
    auto block = dht_a->dht->make_block<ACLBlock>(std::string("reciprocate"));
    dht_a->dht->insert(std::move(block));
  }
  BOOST_CHECK_EQUAL(b1.size(), 1);
  BOOST_CHECK_EQUAL(b2.size(), 1);
}

ELLE_TEST_SCHEDULED(chain_connect,
                    (TestConfiguration, config),
                    (bool, anonymous),
                    (bool, sync))
{
  infinit::storage::Memory::Blocks b1, b2;
  auto id_a = special_id(10);
  auto id_b = special_id(11);
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  ELLE_LOG("create DHTs");
  auto dht_a = std::make_unique<DHT>(
    ::id = id_a,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b1));
  auto dht_b = std::make_unique<DHT>(
    ::id = id_b,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b2));
  ELLE_LOG("connect DHTs")
    discover(*dht_b, *dht_a, anonymous, false, sync, sync);
  auto client = DHT(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    ::storage = nullptr);
  ELLE_LOG("connect client")
  {
    auto discovered_a = elle::reactor::waiter(
      client.dht->overlay()->on_discovery(),
      [&] (NodeLocation const& l, bool) { return l.id() == id_a; });
    auto discovered_b = elle::reactor::waiter(
      client.dht->overlay()->on_discovery(),
      [&] (NodeLocation const& l, bool) { return l.id() == id_b; });
    ELLE_LOG("connect client")
      discover(client, *dht_a, anonymous);
    elle::reactor::wait({discovered_a, discovered_b});
  }
  ELLE_LOG("store block")
  {
    auto block = client.dht->make_block<ACLBlock>(
      std::string("chain_connect"));
    client.dht->insert(std::move(block));
    BOOST_CHECK_EQUAL(b1.size(), 1);
    BOOST_CHECK_EQUAL(b2.size(), 1);
  }
  ELLE_LOG("restart second DHT")
  {
    {
      auto disappeared_a = elle::reactor::waiter(
        dht_a->dht->overlay()->on_disappearance(),
        [&] (Address id, bool) { return id == id_b; });
      auto disappeared_client = elle::reactor::waiter(
        client.dht->overlay()->on_disappearance(),
        [&] (Address id, bool) { return id == id_b; });
      dht_b.reset();
      elle::reactor::wait({disappeared_a, disappeared_client});
    }
    {
      auto connected_client = elle::reactor::waiter(
        client.dht->overlay()->on_discovery(),
        [&] (NodeLocation const& l, bool) { return l.id() == id_b; });
      dht_b = std::make_unique<DHT>(
        ::id = id_b,
        ::version = config.version,
        ::keys = keys,
        ::make_overlay = config.overlay_builder,
        dht::consensus::rebalance_auto_expand = false,
        ::storage = std::make_unique<infinit::storage::Memory>(b2));
      discover(*dht_b, *dht_a, anonymous, sync, sync);
      elle::reactor::wait(connected_client);
    }
  }
  ELLE_LOG("store block")
  {
    auto block = client.dht->make_block<ACLBlock>(
      std::string("chain_connect"));
    client.dht->insert(std::move(block));
    BOOST_CHECK_EQUAL(b1.size(), 2);
    BOOST_CHECK_EQUAL(b2.size(), 2);
  }
}

ELLE_TEST_SCHEDULED(
  key_cache_invalidation, (TestConfiguration, config), (bool, anonymous))
{
  infinit::storage::Memory::Blocks blocks;
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto id_a = infinit::model::Address::random();
  auto dht_a = std::make_unique<DHT>(
    ::id = id_a,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::paxos = false,
    ::protocol = infinit::model::doughnut::Protocol::utp,
    ::storage = std::make_unique<infinit::storage::Memory>(blocks));
  int port = dht_a->dht->local()->server_endpoints().begin()->port();
  DHT dht_b(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    ::storage = nullptr,
    ::paxos = false,
    ::protocol = infinit::model::doughnut::Protocol::utp);
  discover(dht_b, *dht_a, anonymous, false, true);
  auto block = dht_a->dht->make_block<ACLBlock>(std::string("block"));
  auto& acb = dynamic_cast<infinit::model::doughnut::ACB&>(*block);
  acb.set_permissions(elle::cryptography::rsa::keypair::generate(512).K(),
    true, true);
  acb.set_permissions(elle::cryptography::rsa::keypair::generate(512).K(),
    true, true);
  dht_a->dht->seal_and_insert(*block, tcr());
  auto b2 = dht_b.dht->fetch(block->address());
  dynamic_cast<MutableBlock*>(b2.get())->data(elle::Buffer("foo"));
  dht_b.dht->seal_and_update(*b2, tcr());
  // brutal restart of a
  ELLE_LOG("disconnect A");
  dht_a->dht->local()->utp_server()->socket()->close();
  ELLE_LOG("recreate A");
  auto dht_aa = std::make_unique<DHT>(
    ::id = id_a,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::paxos = false,
    ::protocol = infinit::model::doughnut::Protocol::utp,
    ::storage = std::make_unique<infinit::storage::Memory>(blocks),
    ::port = port);
  // rebind local somewhere else or we get EBADF from local_endpoint
  dht_a->dht->local()->utp_server()->socket()->bind(
    boost::asio::ip::udp::endpoint(
      boost::asio::ip::address::from_string("127.0.0.1"), 0));
  // Force a reconnection
  auto peer = dht_b.dht->dock().peer_cache().begin()->lock();
  ELLE_LOG("disconnect from A")
    dynamic_cast<infinit::model::doughnut::Remote&>(*peer).disconnect();
  ELLE_LOG("reconnect to A")
    dynamic_cast<infinit::model::doughnut::Remote&>(*peer).connect();
  ELLE_LOG("re-store block");
  dynamic_cast<MutableBlock*>(b2.get())->data(elle::Buffer("foo"));
  BOOST_CHECK_NO_THROW(dht_b.dht->seal_and_update(*b2, tcr()));
  ELLE_LOG("test end");
}

ELLE_TEST_SCHEDULED(
  chain_connect_doom, (TestConfiguration, config), (bool, anonymous))
{
  infinit::storage::Memory::Blocks b1, b2, b3;
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto id_a = infinit::model::Address::random();
  ELLE_LOG("create DHTs");
  auto dht_a = std::make_unique<DHT>(
    ::id = id_a,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::paxos = false,
    dht::consensus::rebalance_auto_expand = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b1));
  auto dht_b = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false,
    ::paxos = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b2));
  auto dht_c = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false,
    ::paxos = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b3));
  ELLE_LOG("connect DHTs")
  {
    discover(*dht_b, *dht_a, anonymous, false, true, true);
    discover(*dht_c, *dht_b, anonymous, false, true, true);
  }
  unsigned int pa = 0, pb = 0, pc = 0;
  for (auto tgt: {dht_a.get(), dht_b.get(), dht_c.get()})
  {
    ELLE_LOG("store blocks in %s", tgt);
    auto client = std::make_unique<DHT>(
      ::keys = keys,
      ::version = config.version,
      ::make_overlay = config.overlay_builder,
      ::paxos = false,
      ::storage = nullptr);
    discover(*client, *tgt, anonymous, false, true);
    auto addrs = std::vector<infinit::model::Address>{};
    for (int a = 0; a < 20; ++a)
    {
      try
      {
        for (int i = 0; i < 5; ++i)
        {
          auto block = client->dht->make_block<ACLBlock>(std::string("block"));
          addrs.push_back(block->address());
          client->dht->insert(std::move(block), tcr());
        }
      }
      catch (elle::Error const& e)
      {
        ELLE_ERR("Exception storing blocks: %s", e);
        throw;
      }
      if (b1.size() - pa >= 5
          && b2.size() - pb >= 5
          && b3.size() - pc >=5)
        break;
    }
    ELLE_LOG("stores: %s %s %s", b1.size(), b2.size(), b3.size());
    BOOST_TEST(b1.size() - pa >= 5);
    BOOST_TEST(b2.size() - pb >= 5);
    BOOST_TEST(b3.size() - pc >= 5);
    pa = b1.size();
    pb = b2.size();
    pc = b3.size();
    for (auto const& a: addrs)
      client->dht->fetch(a);
    ELLE_LOG("teardown client");
  }
  ELLE_LOG("teardown");
}

ELLE_TEST_SCHEDULED(
  data_spread, (TestConfiguration, config), (bool, anonymous))
{
  infinit::storage::Memory::Blocks b1, b2;
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto id_a = infinit::model::Address::random();
  auto dht_a = std::make_unique<DHT>(
    ::id = id_a,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::paxos = false,
    dht::consensus::rebalance_auto_expand = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b1));
  auto dht_b = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false,
    ::paxos = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b2));
  ELLE_LOG("server discovery");
  discover(*dht_b, *dht_a, anonymous);
  // client. Hard-coded replication_factor=3 if paxos is enabled
  auto client = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    ::paxos = false,
    ::storage = nullptr);
  ELLE_LOG("client discovery");
  auto discovered_client_b = elle::reactor::waiter(
    client->dht->overlay()->on_discovery(),
    [&] (NodeLocation const& l, bool) { return l.id() == dht_b->dht->id(); });
  discover(*client, *dht_a, anonymous, false, true);
  elle::reactor::wait(discovered_client_b);
  std::vector<infinit::model::Address> addrs;
  ELLE_LOG("writing blocks");
  for (int a=0; a<10; ++a)
  {
    for (int i=0; i<50; ++i)
    {
      auto block = dht_a->dht->make_block<ACLBlock>(std::string("block"));
      addrs.push_back(block->address());
      client->dht->insert(std::move(block), tcr());
    }
    if (b1.size() >=5 && b2.size() >=5)
      break;
  }
  ELLE_LOG("stores: %s %s", b1.size(), b2.size());
  BOOST_CHECK_GE(b1.size(), 5);
  BOOST_CHECK_GE(b2.size(), 5);
  for (auto const& a: addrs)
    client->dht->fetch(a);
}

ELLE_TEST_SCHEDULED(
  data_spread2, (TestConfiguration, config), (bool, anonymous))
{
  infinit::storage::Memory::Blocks b1, b2;
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto id_a = infinit::model::Address::random();
  auto dht_a = std::make_unique<DHT>(
    ::id = id_a,
    ::version = config.version,
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::paxos = false,
    dht::consensus::rebalance_auto_expand = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b1));
  auto dht_b = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false,
    ::paxos = false,
    ::storage = std::make_unique<infinit::storage::Memory>(b2));
  auto client = std::make_unique<DHT>(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    ::paxos = false,
    ::storage = nullptr);
  discover(*client, *dht_a, anonymous, false, true);
  discover(*dht_a, *dht_b, anonymous, false, true);
  auto addrs = std::vector<infinit::model::Address>{};
  for (int a=0; a<10; ++a)
  {
    for (int i=0; i<50; ++i)
    {
      auto block = dht_a->dht->make_block<ACLBlock>(std::string("block"));
      addrs.push_back(block->address());
      client->dht->insert(std::move(block), tcr());
    }
    if (b1.size() >= 5 && b2.size() >= 5)
      break;
  }
  ELLE_LOG("stores: %s %s", b1.size(), b2.size());
  BOOST_TEST(b1.size() >= 5);
  BOOST_TEST(b2.size() >= 5);
  for (auto const& a: addrs)
    client->dht->fetch(a);
  ELLE_LOG("teardown");
}

ELLE_TEST_SCHEDULED(
  storm, (TestConfiguration, config),
  (bool, pax), (int, nservers), (int, nclients), (int, nactions))
{
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  bool is_kelips = false;
  auto servers = std::vector<std::unique_ptr<DHT>>{};
  ELLE_LOG("setup %s servers", nservers)
    for (int i = 0; i < nservers; ++i)
    {
      auto dht = std::make_unique<DHT>(
        ::id = special_id(i + 1),
        ::version = config.version,
        ::keys = keys,
        make_overlay = config.overlay_builder,
        paxos = pax,
        dht::consensus::rebalance_auto_expand = false
      );
      if (auto kelips = get_kelips(*dht))
      {
        is_kelips = true;
        kelips->config().query_put_retries = 6;
        kelips->config().query_timeout_ms = valgrind(2000, 4);
        kelips->config().contact_timeout_ms = valgrind(100000,20);
        kelips->config().ping_interval_ms = valgrind(500, 10);
        kelips->config().ping_timeout_ms = valgrind(2000, 20);
      }
      servers.emplace_back(std::move(dht));
    }
  ELLE_LOG("connect servers")
    if (is_kelips)
      for (int i = 1; i < nservers; ++i)
        discover(*servers[i], *servers[0], false, true);
    else
      for (int i = 1; i < nservers; ++i)
        for (int j = 0; j < i; ++j)
          discover(*servers[i], *servers[j], false, true);
  ELLE_LOG("waiting for servers")
    for (int i = 1; i < nservers; ++i)
      for (int j = 0; j < i; ++j)
        if (!servers[i]->dht->overlay()->discovered(servers[j]->dht->id()))
          elle::reactor::wait(
            servers[i]->dht->overlay()->on_discovery(),
            [&] (NodeLocation const& l, bool)
            {
              return l.id() == servers[j]->dht->id();
            });
  auto clients = std::vector<std::unique_ptr<DHT>>{};
  ELLE_LOG("set up clients")
    for (int i = 0; i < nclients; ++i)
    {
      auto dht = std::make_unique<DHT>(
        ::keys = keys,
        ::version = config.version,
        ::make_overlay = config.overlay_builder,
        ::paxos = pax,
        ::storage = nullptr,
        dht::consensus::rebalance_auto_expand = false
        );
      if (auto kelips = get_kelips(*dht))
      {
        kelips->config().query_put_retries = 6;
        kelips->config().query_get_retries = 20;
        kelips->config().query_timeout_ms = valgrind(2000, 4);
        kelips->config().contact_timeout_ms = valgrind(100000,20);
        kelips->config().ping_interval_ms = valgrind(500, 10);
        kelips->config().ping_timeout_ms = valgrind(2000, 20);
      }
      clients.emplace_back(std::move(dht));
    }
  ELLE_LOG("connect clients")
    for (int i = 0; i < nservers; ++i)
      for (int j = 0; j < nclients; ++j)
        if (!clients[j]->dht->overlay()->discovered(servers[i]->dht->id()))
          ELLE_TRACE("client %s discovers server %s", j, i)
            discover(*clients[j], *servers[i], false, false, true);
  ELLE_LOG("run some tests thingy, cf bearclaw");
  auto addrs = std::vector<infinit::model::Address>{};
  elle::With<elle::reactor::Scope>() << [&](elle::reactor::Scope& s)
  {
    for (auto& c: clients)
      s.run_background(elle::sprintf("storm %s", c), [&] {
        try
        {
          for (int i=0; i<nactions; ++i)
          {
            int r = rand()%100;
            ELLE_TRACE_SCOPE("action %s: %s, with %s addrs",
              i, r, addrs.size());
            if (r < 20 && !addrs.empty())
            {
              // delete
              int p = rand()%addrs.size();
              auto addr = addrs[p];
              ELLE_DEBUG("deleting %f", addr);
              std::swap(addrs[p], addrs[addrs.size()-1]);
              addrs.pop_back();
              try
              {
                c->dht->remove(addr);
              }
              catch (infinit::model::MissingBlock const& mb)
              {
              }
              catch (elle::athena::paxos::TooFewPeers const& tfp)
              {
              }
              ELLE_DEBUG("deleted %f", addr);
            }
            else if (r < 50 || addrs.empty())
            {
              // create
              ELLE_DEBUG("creating");
              auto block = c->dht->make_block<ACLBlock>(std::string("block"));
              auto a = block->address();
              try
              {
                c->dht->insert(std::move(block), tcr());
              }
              catch (elle::Error const& e)
              {
                ELLE_ERR("insertiong of %s failed with %s", a, e);
                throw;
              }
              ELLE_DEBUG("created %f", a);
              addrs.push_back(a);
            }
            else
            {
              // read
              auto addr = get_random(addrs);
              ELLE_DEBUG_SCOPE("reading %f", addr);
              std::exception_ptr except;
              try
              {
                auto block = c->dht->fetch(addr);
                if (r < 80)
                {
                  // Update.
                  ELLE_DEBUG("updating %f", addr);
                  auto aclb
                    = dynamic_cast<infinit::model::blocks::ACLBlock*>(block.get());
                  aclb->data(elle::Buffer("coincoin"));
                  try
                  {
                    c->dht->update(std::move(block), tcr());
                  }
                  catch (elle::Error const& e)
                  {
                    ELLE_ERR("update of %s failed with %s", addr, e);
                    throw;
                  }
                  ELLE_DEBUG("updated %f", addr);
                }
              }
              catch (infinit::model::MissingBlock const& mb)
              {
                except = std::current_exception();
              }
              catch (elle::athena::paxos::TooFewPeers const& tfp)
              {
                except = std::current_exception();
              }
              catch (elle::Error const& e)
              {
                // also intercept "no peer available for..." exceptions,
                // which currently are not typed(FIXME when they are).
                if (std::string(e.what()).find("no peer available for") == std::string::npos)
                  throw;
                except = std::current_exception();
              }
              if (except)
              {
                // This can be legit if a delete crossed our path
                if (elle::contains(addrs, addr))
                {
                  ELLE_ERR("exception on supposedly live block %f: %s",
                           addr, elle::exception_string(except));
                  std::rethrow_exception(except);
                }
              }
              ELLE_DEBUG("read %f", addr);
            }
            ELLE_TRACE("terminated action %s", i);
          }
        }
        catch (elle::Error const& e)
        {
          ELLE_ERR("%s: exception %s at %s", c, e, e.backtrace());
          throw;
        }
      });
    elle::reactor::wait(s);
    ELLE_TRACE("exiting scope");
  };
  ELLE_TRACE("teardown");
}

ELLE_TEST_SCHEDULED(
  parallel_discover, (TestConfiguration, config), (bool, anonymous))
{
  constexpr auto nservers = 5;
  constexpr auto npeers = nservers - 1;
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto servers = std::vector<std::unique_ptr<DHT>>{};
  for (int i=0; i<nservers; ++i)
  {
    auto dht = std::make_unique<DHT>(
      ::keys = keys,
      ::version = config.version,
      ::make_overlay = config.overlay_builder,
      ::paxos = false);
    servers.emplace_back(std::move(dht));
  }
  elle::With<elle::reactor::Scope>() << [&](elle::reactor::Scope& s)
  {
    for (int i=0; i<rand()%5; ++i)
      elle::reactor::yield();
    for (int i=1; i<nservers; ++i)
      s.run_background("discover", [&,i] {
          discover(*servers[i], *servers[0], anonymous);
      });
    elle::reactor::wait(s);
  };
  // Number of servers that know all their peers.
  auto c = 0;
  // Previously we limit ourselves to 50 attempts.  When run
  // repeatedly, it did happen to fail for lack of time.
  for (auto i = 0; i < 100 && c != nservers; ++i)
  {
    elle::reactor::sleep(100_ms);
    c = boost::count_if(servers, [npeers](auto&& s) {
        return peer_count(*s) == npeers;
      });
  }
  BOOST_CHECK_EQUAL(c, nservers);
}

ELLE_TEST_SCHEDULED(
  change_endpoints,
  (TestConfiguration, config),
  (bool, anonymous),
  (bool, back))
{
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto storage = infinit::storage::Memory::Blocks();
  ELLE_LOG("create DHTs");
  auto dht_a = std::make_unique<DHT>(
    ::id = special_id(10),
    ::version = config.version,
    ::keys = keys,
    make_overlay = config.overlay_builder,
    dht::consensus::rebalance_auto_expand = false);
  auto dht_b = std::make_unique<DHT>(
    ::id = special_id(11),
    ::version = config.version,
    ::keys = keys,
    make_overlay = config.overlay_builder,
    ::storage = std::make_unique<infinit::storage::Memory>(storage),
    dht::consensus::rebalance_auto_expand = false);
  ELLE_LOG("connect DHTs")
    discover(*dht_a, *dht_b, anonymous, false, true, true);
  infinit::model::Address addr;
  {
    auto block =
      dht_a->dht->make_block<ACLBlock>(std::string("change_endpoints"));
    addr = block->address();
    dht_a->dht->insert(std::move(block));
  }
  // Keep the remote alive and change the endpoints.
  auto remote = dht_a->dht->overlay()->lookup_node(dht_b->dht->id()).lock();
  BOOST_CHECK_EQUAL(remote->id(), dht_b->dht->id());
  BOOST_CHECK_EQUAL(dht_a->dht->fetch(addr)->data(), "change_endpoints");
  ELLE_LOG("recreate second DHT")
  {
    auto disappear_b = elle::reactor::waiter(
      dht_a->dht->overlay()->on_disappearance(),
      [&] (Address id, bool) { return id == special_id(11); });
    dht_b.reset();
    elle::reactor::wait(disappear_b);
    dht_b = std::make_unique<DHT>(
      ::id = special_id(11),
      ::version = config.version,
      ::keys = keys,
      ::make_overlay = config.overlay_builder,
      ::storage = std::make_unique<infinit::storage::Memory>(storage),
      dht::consensus::rebalance_auto_expand = false);
  }
  ELLE_LOG("reconnect second DHT")
    if (back)
      discover(*dht_b, *dht_a, anonymous, false, true, true);
    else
      discover(*dht_a, *dht_b, anonymous, false, true, true);
  BOOST_CHECK_EQUAL(dht_a->dht->fetch(addr)->data(), "change_endpoints");
}

ELLE_TEST_SCHEDULED(
  change_endpoints_stale,
  (TestConfiguration, config),
  (bool, back))
{
  auto storage = infinit::storage::Memory::Blocks();
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto make_dht_a = [&]
    {
      return std::make_unique<DHT>(
        ::id = special_id(10),
        ::version = config.version,
        ::keys = keys,
        ::make_overlay = config.overlay_builder,
        dht::consensus::rebalance_auto_expand = false,
        ::storage = std::make_unique<infinit::storage::Memory>(storage));
    };
  auto dht_a = make_dht_a();
  elle::With<TCPInstrument>(
    dht_a->dht->local()->server_endpoints().begin()->port()) <<
    [&] (TCPInstrument& instrument)
    {
      auto dht_b = DHT(
        ::id = special_id(11),
        ::version = config.version,
        ::keys = keys,
        ::make_overlay = config.overlay_builder,
        doughnut::consensus::rebalance_auto_expand = false,
        doughnut::connect_timeout = std::chrono::milliseconds(valgrind(100, 10)),
        doughnut::soft_fail_running = true);
      auto loc = NodeLocation(dht_a->dht->id(), {instrument.endpoint()});
      Address addr;
      // Ensure one request can go through.
      {
        auto block = dht_a->dht->make_block<MutableBlock>(std::string("stale"));
        addr = block->address();
        ELLE_LOG("store block")
          dht_a->dht->seal_and_insert(*block, tcr());
      }
      ELLE_LOG("connect DHTs")
        discover(dht_b, *dht_a, loc, true);
      ELLE_LOG("lookup block")
        BOOST_CHECK_EQUAL(dht_b.dht->fetch(addr)->data(), "stale");
      ELLE_LOG("stale connection")
      {
        instrument.transmission().close();
        dht_a.reset();
      }
      ELLE_LOG("check we can't lookup the block")
        BOOST_CHECK_THROW(dht_b.dht->fetch(addr)->data(),
                          elle::athena::paxos::TooFewPeers);
      ELLE_LOG("reset first DHT")
        dht_a = make_dht_a();
      ELLE_LOG("connect DHTs")
        if (back)
          discover(*dht_a, dht_b, false, true);
        else
          discover(dht_b, *dht_a, false);
      if (get_kouncil(dht_b))
        ELLE_LOG("check we can't lookup the block")
          BOOST_CHECK_THROW(dht_b.dht->fetch(addr)->data(),
                            elle::athena::paxos::TooFewPeers);
      ELLE_LOG("close connection and lookup block")
      {
        instrument.close();
        ELLE_LOG("wait for discover");
        if (get_kouncil(dht_b))
          elle::reactor::wait(
            dht_b.dht->overlay()->on_discovery(),
            [&] (NodeLocation const& l, bool)
            {
              return l.id() == dht_a->dht->id();
            });
        ELLE_LOG("check fetching the block");
        BOOST_CHECK_EQUAL(dht_b.dht->fetch(addr)->data(), "stale");
      }
    };
}

ELLE_TEST_SCHEDULED(
  reboot_node,
  (TestConfiguration, config),
  (bool, anonymous))
{
  // FIXME: test run in TCP only because the BF for some reason takes forever
  // (nearly 30s) to detect stale UTP connections.
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto a = std::make_unique<DHT>(
    ::version = config.version,
    ::id = special_id(10),
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::protocol = dht::Protocol::tcp);
  if (get_kelips(*a))
    return; // kelips cannot handle automatic reconnection after on_disappearance()
  auto b = std::make_unique<DHT>(
    ::version = config.version,
    ::id = special_id(11),
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::protocol = dht::Protocol::tcp);
  int port = b->dht->local()->server_endpoint().port();
  ELLE_LOG("connect DHTs")
    discover(*b, *a, anonymous, false, true, true);
  ELLE_LOG("stop second DHT")
  {
    auto disappeared = elle::reactor::waiter(
      a->dht->overlay()->on_disappearance(),
      [&] (Address id, bool)
      {
        BOOST_TEST(id == special_id(11));
        return true;
      });
    b.reset();
    elle::reactor::wait(disappeared);
  }
  ELLE_LOG("start second DHT on port %s", port)
  {
    auto discovered = elle::reactor::waiter(
      a->dht->overlay()->on_discovery(),
      [&] (NodeLocation const& l, bool)
      {
        BOOST_TEST(l.id() == special_id(11));
        return true;
      });
    b = std::make_unique<DHT>(
      ::version = config.version,
      ::id = special_id(11),
      ::keys = keys,
      ::make_overlay = config.overlay_builder,
      ::port = port,
      ::protocol = dht::Protocol::tcp);
    elle::reactor::wait(discovered);
  }
}

ELLE_TEST_SCHEDULED(
  remove,
  (TestConfiguration, config),
  (bool, anonymous))
{
  auto storage = infinit::storage::Memory::Blocks();
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto a = std::make_unique<DHT>(
    ::version = config.version,
    ::id = special_id(10),
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::protocol = dht::Protocol::tcp,
    dht::consensus::rebalance_auto_expand = false);
  auto id_b = special_id(11);
  auto b = std::make_unique<DHT>(
    ::version = config.version,
    ::id = id_b,
    ::keys = keys,
    ::storage = std::make_unique<infinit::storage::Memory>(storage),
    ::make_overlay = config.overlay_builder,
    ::protocol = dht::Protocol::tcp,
    dht::consensus::rebalance_auto_expand = false);
  auto block = b->dht->make_block<MutableBlock>(std::string("1351"));
  ELLE_LOG("insert block on B")
    b->dht->seal_and_insert(*block, tcr());
  ELLE_LOG("connect DHTs")
    discover(*b, *a, anonymous, false, true, true);
  BOOST_TEST(
    a->dht->overlay()->lookup(block->address()).lock()->id() == b->dht->id());
  ELLE_LOG("remove block on B")
    b->dht->remove(block->address());
  ELLE_LOG("check block disappeared on A")
    try
    {
      while (true)
      {
        a->dht->overlay()->lookup(block->address());
        elle::reactor::yield();
      }
    }
    catch (MissingBlock const&)
    {}
}

ELLE_TEST_SCHEDULED(
  remove_disconnected,
  (TestConfiguration, config),
  (bool, anonymous))
{
  auto storage = infinit::storage::Memory::Blocks();
  auto const keys = elle::cryptography::rsa::keypair::generate(512);
  auto a = std::make_unique<DHT>(
    ::version = config.version,
    ::id = special_id(10),
    ::keys = keys,
    ::make_overlay = config.overlay_builder,
    ::protocol = dht::Protocol::tcp,
    ::paxos = false);
  auto id_b = special_id(11);
  auto b = std::make_unique<DHT>(
    ::version = config.version,
    ::id = id_b,
    ::keys = keys,
    ::storage = std::make_unique<infinit::storage::Memory>(storage),
    ::make_overlay = config.overlay_builder,
    ::protocol = dht::Protocol::tcp,
    ::paxos = false);
  auto block = b->dht->make_block<MutableBlock>(std::string("1351"));
  b->dht->seal_and_insert(*block, tcr());
  ELLE_LOG("connect DHTs")
    discover(*b, *a, anonymous, false, true, true);
  BOOST_TEST(
    a->dht->overlay()->lookup(block->address()).lock()->id() == b->dht->id());
  ELLE_LOG("stop second DHT")
  {
    auto disappeared = elle::reactor::waiter(
      a->dht->overlay()->on_disappearance(),
      [&] (Address id, bool)
      {
        BOOST_TEST(id == id_b);
        return true;
      });
    b.reset();
    elle::reactor::wait(disappeared);
  }
  auto fail = [&]{
    auto res = a->dht->overlay()->lookup(block->address());
    ELLE_LOG("unexpected result: %s", res);
  };
  BOOST_CHECK_THROW(fail(), MissingBlock);
  ELLE_LOG("start second DHT")
  {
    b = std::make_unique<DHT>(
      ::version = config.version,
      ::id = id_b,
      ::keys = keys,
      ::storage = std::make_unique<infinit::storage::Memory>(storage),
      ::make_overlay = config.overlay_builder,
      ::protocol = dht::Protocol::tcp,
      // FIXME: horrible failure if paxos is set to true.  Can't we
      // get something more readable?
      ::paxos = false);
    b->dht->remove(block->address());
    BOOST_CHECK_THROW(b->dht->overlay()->lookup(block->address()),
                      MissingBlock);
    discover(*b, *a, anonymous, false, true, true);
    BOOST_CHECK_THROW(a->dht->overlay()->lookup(block->address()),
                      MissingBlock);
  }
}

/// Factor the creation of a DHT cluster.
struct Cluster
{
  using Self = Cluster;
  Cluster(TestConfiguration const& config, int n = 5)
    : config{config}
    , n{n}
  {
    ids.resize(n);
    ports.resize(n);
    blocks.resize(n);
    servers.resize(n);
    for (int i=0; i<n; ++i)
    {
      ids[i] = special_id(i + 1);
      recreate_server(i, "create");
      ports[i] = servers[i]->dht->local()->server_endpoint().port();
    }
  }

  /// Regenerate the i-th server with the same id, port, and storage.
  void
  recreate_server(int i, std::string const& log = "recreate")
  {
    ELLE_LOG("%s server %s", log, i);
    servers[i] = std::make_unique<DHT>(
        ::id = ids[i],
        ::version = config.version,
        ::keys = keys,
        ::make_overlay = config.overlay_builder,
        ::paxos = true,
        ::port = ports[i],
        ::storage = std::make_unique<infinit::storage::Memory>(blocks[i]));
  }


  /// Let members discover themselves.
  void
  discover()
  {
    for (int i=0; i<n; ++i)
      for (int j=i+1; j<n; ++j)
        ::discover(*servers[i], *servers[j], false);
  }

  auto begin() { return servers.begin(); }
  auto end() { return servers.end(); }

  TestConfiguration const& config;
  /// Number of machines.
  int n;
  using Keys = elle::cryptography::rsa::KeyPair;
  Keys keys = elle::cryptography::rsa::keypair::generate(512);
  std::vector<infinit::model::Address> ids;
  std::vector<unsigned short> ports;
  std::vector<infinit::storage::Memory::Blocks> blocks;
  std::vector<std::unique_ptr<DHT>> servers;
};


ELLE_TEST_SCHEDULED(churn, (TestConfiguration, config),
  (bool, keep_port), (bool, wait_disconnect), (bool, wait_connect))
{
  auto cluster = Cluster{config};
  auto const n = cluster.n;
  auto& blocks = cluster.blocks;
  auto const& ids = cluster.ids;
  auto const& keys = cluster.keys;
  auto const& ports = cluster.ports;
  auto& servers = cluster.servers;

  cluster.discover();

  auto client = std::make_unique<DHT>(
      ::keys = keys,
      ::version = config.version,
      ::make_overlay = config.overlay_builder,
      ::paxos = true,
      ::storage = nullptr);
  if (auto kelips = get_kelips(*client))
    kelips->config().query_put_retries = 6;
  // We will shoot some servers.
  discover(*client, servers[0] ? *servers[0] : *servers[1], false);
  for (auto& s: servers)
    hard_wait(*s, n-1, client->dht->id());
  hard_wait(*client, n, client->dht->id());
  auto addrs = std::vector<infinit::model::Address>{};
  // The index in servers[] of a host we will disconnect, and later
  // reconnect.
  int down = -1;
  try
  {
  for (int i=1; i < 500 / valgrind(1, 2); ++i)
  {
    if (!(i%100))
    {
      ELLE_LOG("bringing node %s up with %s/%s block",
               down, blocks[down].size(), addrs.size());
      servers[down] = std::make_unique<DHT>(
        ::keys = keys,
        ::version = config.version,
        ::make_overlay = config.overlay_builder,
        ::paxos = true,
        ::id = ids[down],
        ::port = keep_port ? ports[down] : 0,
        ::storage = std::make_unique<infinit::storage::Memory>(blocks[down]));
      for (int s=0; s< n; ++s)
        if (s != down)
        {
          discover(*servers[down], *servers[s], false);
          // Cheating a bit...
          discover(*servers[s], *servers[down], false);
        }
      if (wait_connect)
      {
        for (auto& s: servers)
          hard_wait(*s, n-1, client->dht->id());
        hard_wait(*client, n, client->dht->id());
        ELLE_LOG("resuming");
      }
      down = -1;
    }
    else if (!(i%50))
    {
      ELLE_ASSERT(down == -1);
      down = rand()%n;
      ELLE_LOG("bringing node %s down with %s/%s blocks",
               down, blocks[down].size(), addrs.size());
      servers[down].reset();
      if (wait_disconnect)
      {
        for (auto& s: servers)
          if (s)
            hard_wait(*s, n-2, client->dht->id(), false);
          hard_wait(*client, n-1, client->dht->id(), false);
      }
      ELLE_LOG("resuming");
    }
    if (addrs.empty() || !(i%5))
    {
      auto block = client->dht->make_block<ACLBlock>(std::string("block"));
      auto a = block->address();
      client->dht->insert(std::move(block), tcr());
      ELLE_DEBUG("created %f", a);
      addrs.push_back(a);
    }
    auto a = get_random(addrs);
    auto block = client->dht->fetch(a);
    if (i%2)
    {
      dynamic_cast<infinit::model::blocks::ACLBlock*>(block.get())->data(
        elle::Buffer("coincoin"));
      client->dht->update(std::move(block), tcr());
    }
  }
  }
  catch (...)
  {
    ELLE_ERR("Exception from test: %s", elle::exception_string());
    throw;
  }
}

void
test_churn_socket(TestConfiguration config, bool pasv)
{
  auto cluster = Cluster{config};
  auto const n = cluster.n;
  auto const& keys = cluster.keys;
  auto& servers = cluster.servers;

  // Let the servers discover themselves.
  cluster.discover();
  for (auto& s: servers)
    hard_wait(*s, n-1);

  // A client.
  auto client = DHT(
    ::keys = keys,
    ::version = config.version,
    ::make_overlay = config.overlay_builder,
    ::paxos = true,
    ::storage = nullptr);
  if (auto kelips = get_kelips(client))
  {
    kelips->config().query_put_retries = 6;
    kelips->config().query_timeout_ms = valgrind(1000, 4);
  }
  // Wait for it to discover the first server.
  discover(client, *servers[0], false);
  hard_wait(client, n, client.dht->id());

  // Write some blocks.
  auto addrs = std::vector<infinit::model::Address>{};
  for (int i=0; i<50; ++i)
  {
    auto block = client.dht->make_block<ACLBlock>(std::string("block"));
    auto a = block->address();
    client.dht->insert(std::move(block), tcr());
    ELLE_DEBUG("created %f", a);
    addrs.push_back(a);
  }

  for (int k=0; k < 3 / valgrind(1, 3); ++k)
  {
    ELLE_TRACE("shooting connections");
    // Shoot some connections.
    for (int i = 0; i < 5; ++i)
    {
      auto& peers = servers[i]->dht->local()->peers();
      for (int l = 0; l < 3; ++l)
        close_connection(*get_random(peers));
    }
    if (pasv)
    {
      // Give it time to notice sockets went down.
      for (int i = 0; i < 10; ++i)
        elle::reactor::yield();
      ELLE_TRACE("hard_wait servers");
      for (auto& s: servers)
        kouncil_wait_pasv(*s, n-1);
      ELLE_TRACE("hard_wait client");
      kouncil_wait_pasv(client, n);
    }
    else
    {
      ELLE_TRACE("hard_wait servers");
      for (auto& s: servers)
        hard_wait(*s, n-1);
      ELLE_TRACE("hard_wait client");
      hard_wait(client, n, client.dht->id());
    }
    ELLE_TRACE("checking");
    for (auto const& a: addrs)
      client.dht->fetch(a);
   }
   BOOST_CHECK(true);
}

ELLE_TEST_SCHEDULED(churn_socket, (TestConfiguration, config))
{
  test_churn_socket(config, false);
}

ELLE_TEST_SCHEDULED(churn_socket_pasv, (TestConfiguration, config))
{
  test_churn_socket(config, true);
}

ELLE_TEST_SCHEDULED(eviction, (TestConfiguration, config))
{
  auto cluster = Cluster{config, 3};
  auto const& ids = cluster.ids;
  auto& servers = cluster.servers;
  /// Let's not wait eviction for too long.
  for (auto& c: cluster)
    get_kouncil(*c)->eviction_delay(std::chrono::seconds{valgrind(1, 5) * 10});

  /// A: the main peer.
  auto& dht_a = servers[0];
  auto const id_a = ids[0];

  /// B: a peer connected to A.
  auto& dht_b = servers[1];
  auto const id_b = ids[1];
  ELLE_LOG("connect A and B")
    discover(*dht_b, *dht_a, false);
  ELLE_LOG("wait for A to see B")
    hard_wait(*dht_a, 1);
  ELLE_LOG("Peers of A (%s): %f", dht_a, get_peers(*dht_a));
  ELLE_LOG("Peers of B (%s): %f", dht_b, get_peers(*dht_b));

  // Shoot B.
  ELLE_LOG("shooting B")
  {
    auto b_disappeared = elle::reactor::waiter(
      dht_a->dht->overlay()->on_disappearance(),
      [&] (Address id, bool)
      {
        BOOST_TEST(id == id_b);
        return true;
      });
    dht_b = nullptr;
    elle::reactor::wait(b_disappeared);
  }
  ELLE_LOG("A knows B disappeared")

  // A no longer sees B.
  {
    auto ps = get_peers(*dht_a);
    ELLE_LOG("Peers of A (%s): %f", dht_a, ps);
    CHECK_NOT_IN(id_b, ps);
  }

  // C: A new peer, connected to A.  Node A should tell C about B.
  auto& dht_c = servers[2];
  ELLE_LOG("bringing node C up")
    discover(*dht_c, *dht_a, false);
  ELLE_LOG("wait for C to discover A")
    hard_wait(*dht_c, 1);
  // Check that C knows that B existed.
  {
    auto peers = get_peers(*dht_c);
    ELLE_LOG("Peers of C (%s): %f", dht_c, peers);
    // B and C are not connected.
    CHECK_IN(id_a, peers);
    CHECK_NOT_IN(id_b, peers);

    auto addrs = get_peers(*dht_c, "infos");
    ELLE_LOG("Infos of C (%s): %f", dht_c, addrs);
    CHECK_IN(id_b, addrs);
  }

  // Let some time pass.
  elle::reactor::sleep(2_sec);

  // Kill server A, C remains alone, remembering about A and B.
  {
    ELLE_LOG("kill A and wait for C to notice");
    auto a_disappeared = elle::reactor::waiter(
      dht_c->dht->overlay()->on_disappearance(),
      [&] (Address id, bool)
      {
        BOOST_TEST(id == id_a);
        return true;
      });
    dht_a = nullptr;
    ELLE_LOG("wait for C to notice A disappeared");
    elle::reactor::wait(a_disappeared);
    {
      auto addrs = get_peers(*dht_c, "infos");
      ELLE_LOG("Infos of C (%s): %f", dht_c, addrs);
      CHECK_IN(id_a, addrs);
      CHECK_IN(id_b, addrs);

      auto peers = get_peers(*dht_c);
      ELLE_LOG("Peers of C (%s): %f", dht_c, peers);
      CHECK_NOT_IN(id_a, peers);
      CHECK_NOT_IN(id_b, peers);
    }
  }
  // Recreate B, and expect C to connect to it, although they have
  // never been in touch yet.
  {
    auto b_appeared = elle::reactor::waiter(
      dht_c->dht->overlay()->on_discovery(),
      [&] (NodeLocation const& l, bool)
      {
        BOOST_TEST(l.id() == id_b);
        return true;
      });
    cluster.recreate_server(1);
    ELLE_LOG("wait for C to see B")
      elle::reactor::wait(b_appeared);
    {
      // C heard about A and B.
      auto addrs = get_peers(*dht_c, "infos");
      ELLE_LOG("Infos of C (%s): %f", dht_c, addrs);
      CHECK_IN(id_a, addrs);
      CHECK_IN(id_b, addrs);

      // And now C is in touch with B.
      auto peers = get_peers(*dht_c);
      ELLE_LOG("Peers of C (%s): %f", dht_c, peers);
      CHECK_NOT_IN(id_a, peers);
      CHECK_IN(id_b, peers);
    }
  }
  /// FIXME: why do I need this???  Who sent std::hex to std::cerr?
  std::cerr << std::dec;
  ELLE_LOG("wait for C to evict A")
  {
    {
      // C still knows about A and B.
      auto addrs = get_peers(*dht_c, "infos");
      ELLE_LOG("Infos of C (%s): %f", dht_c, addrs);
      CHECK_IN(id_a, addrs);
      CHECK_IN(id_b, addrs);
    }
    auto a_evicted = elle::reactor::waiter(
      get_kouncil(*dht_c)->on_eviction(),
      [&] (Address id)
      {
        BOOST_TEST(id == id_a);
        return true;
      });
    elle::reactor::wait(a_evicted);
    {
      auto addrs = get_peers(*dht_c, "infos");
      CHECK_NOT_IN(id_a, addrs);
      CHECK_IN(id_b, addrs);
    }
  }
}

ELLE_TEST_SUITE()
{
  static int windows_factor =
#ifdef INFINIT_WINDOWS
    5;
#else
    1;
#endif

  // elle::os::setenv("INFINIT_CONNECT_TIMEOUT",
  //                  elle::sprintf("%sms", valgrind(100, 20)), 1);
  // elle::os::setenv("INFINIT_SOFTFAIL_TIMEOUT",
  //                  elle::sprintf("%sms", valgrind(100, 20)), 1);
  elle::os::setenv("INFINIT_KOUNCIL_WATCHER_INTERVAL",
                   elle::sprintf("%sms", windows_factor * valgrind(20, 50)), 1);
  elle::os::setenv("INFINIT_KOUNCIL_WATCHER_MAX_RETRY",
                   elle::sprintf("%sms", valgrind(20, 50)), 1);
  auto& master = boost::unit_test::framework::master_test_suite();
  auto const kelips_config = TestConfiguration{
    [] (Doughnut& dht, std::shared_ptr<Local> local)
    {
      auto conf = kelips::Configuration();
      conf.query_get_retries = 4;
      conf.query_put_retries = 4;
      conf.query_put_insert_ttl = 0;
      conf.query_timeout_ms = valgrind(200 * windows_factor, 20);
      conf.contact_timeout_ms = windows_factor * valgrind(500, 20);
      conf.ping_interval_ms = windows_factor * valgrind(20, 10);
      conf.ping_timeout_ms = windows_factor * valgrind(100, 2);
      return std::make_unique<kelips::Node>(
        conf, local, &dht);
    }};

  auto make_kouncil = [](Doughnut& dht, std::shared_ptr<Local> local)
    {
      return std::make_unique<kouncil::Kouncil>(&dht, local);
    };
  auto const kouncil_config
    = TestConfiguration{make_kouncil, elle::Version(0, 8, 0)};
  auto const kouncil_0_7_config
    = TestConfiguration{make_kouncil, elle::Version(0, 7, 0)};


#define BOOST_NAMED_TEST_CASE(name, test_function)                      \
  boost::unit_test::make_test_case(                                     \
    boost::function<void ()>(test_function), name, __FILE__, __LINE__ ) \

#define TEST(Suite, Overlay, Name, Timeout, Function, ...)              \
  Suite->add(BOOST_NAMED_TEST_CASE(                                     \
                 Name,                                                  \
                 [=] { ::Function(BOOST_PP_CAT(Overlay, _config),       \
                                  ##__VA_ARGS__); }),                   \
             0, valgrind(Timeout));                                     \

#define TEST_ANON(Overlay, Name, F, Timeout, ...)                       \
  {                                                                     \
    auto suite = BOOST_TEST_SUITE(#Name);                               \
    Overlay->add(suite);                                                \
    TEST(suite, Overlay, "named", Timeout, F, false,                    \
          ##__VA_ARGS__);                                               \
    TEST(suite, Overlay, "anonymous",  Timeout, F, true,                \
          ##__VA_ARGS__);                                               \
  }                                                                     \

#define TEST_NAMED(Overlay, Name, Func, Timeout, ...)                   \
  TEST(Overlay, Overlay, #Name, Timeout, Func, ##__VA_ARGS__)           \

#define OVERLAY(Name)                                                   \
  auto Name = BOOST_TEST_SUITE(#Name);                                  \
  master.add(Name);                                                     \
  TEST_ANON(Name, basics, basics, 5);                                   \
  TEST_ANON(Name, dead_peer, dead_peer, 5);                             \
  TEST_ANON(Name, discover_endpoints, discover_endpoints, 10);          \
  TEST_ANON(Name, reciprocate, reciprocate, 10);                        \
  TEST_ANON(Name, key_cache_invalidation, key_cache_invalidation, 10);  \
  TEST_ANON(Name, data_spread, data_spread, 30);                        \
  TEST_ANON(Name, data_spread2, data_spread2, 30);                      \
  TEST_ANON(Name, chain_connect_sync, chain_connect, 30, true);         \
  TEST_ANON(Name, chain_connect_async, chain_connect, 30, false);       \
  TEST_ANON(Name, parallel_discover, parallel_discover, 20);            \
  TEST_ANON(Name, change_endpoints_back, change_endpoints, 20, true);   \
  TEST_ANON(Name, change_endpoints_forth, change_endpoints, 20, false); \
  TEST_NAMED(                                                           \
    Name, change_endpoints_stale_forth, change_endpoints_stale,         \
    20, false);                                                         \
  TEST_NAMED(                                                           \
    Name, change_endpoints_stale_back, change_endpoints_stale,          \
    20, true);                                                          \
  TEST_ANON(Name, reboot, reboot_node, 5);                              \
  /* long, wild tests*/                                                 \
  TEST_ANON(Name, chain_connect_doom, chain_connect_doom, 30);          \
  TEST_NAMED(Name, storm_paxos, storm, 60, true, 5, 5, 100);            \
  TEST_NAMED(Name, storm,       storm, 60, false, 5, 5, 200);           \
  TEST_NAMED(Name, churn, churn, 600, false, true, true);               \
  TEST_NAMED(Name, churn_socket, churn_socket, 600);                    \

  OVERLAY(kelips);
  OVERLAY(kouncil);
  OVERLAY(kouncil_0_7);
#undef OVERLAY

  // TEST_NAMED(kouncil, eviction, eviction, 600);
  TEST(kouncil, kouncil, "churn_socket_pasv", 30, churn_socket_pasv);
  // FIXME: Kouncil < 0.8 does not handle removal, but kelips should pass those
  // two tests.
  TEST(kouncil, kouncil, "remove", 5, remove, false);
  TEST(kouncil, kouncil, "remove_disconnected", 5, remove_disconnected, false);
}
