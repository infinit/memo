#include <elle/test.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/overlay/kouncil/Kouncil.hh>
#include <infinit/overlay/kelips/Kelips.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("infinit.overlay.test");

using namespace infinit::model;
using namespace infinit::model::blocks;
using namespace infinit::model::doughnut;
using namespace infinit::overlay;

class UTPInstrument
{
public:
  UTPInstrument(infinit::model::Endpoint endpoint)
    : server()
    , _endpoint(std::move(endpoint))
    , _thread(new reactor::Thread(elle::sprintf("%s server", this),
                                  std::bind(&UTPInstrument::_serve, this)))
  {
    this->server.listen(0);
    this->_transmission.open();
  }

  reactor::network::UTPServer server;
  ELLE_ATTRIBUTE_RX(reactor::Barrier, transmission);

private:
  ELLE_ATTRIBUTE(infinit::model::Endpoint, endpoint);
  void
  _serve()
  {
    elle::With<reactor::Scope>() << [this] (reactor::Scope& s)
    {
      while (true)
      {
        auto socket = std::shared_ptr<reactor::network::UTPSocket>(
          this->server.accept().release());
        auto target = std::make_shared<reactor::network::UTPSocket>(
          server, "127.0.0.1", this->_endpoint.port());
        auto transmit =
          [this] (std::shared_ptr<reactor::network::UTPSocket> from,
                  std::shared_ptr<reactor::network::UTPSocket> to)
          {
            try
            {
              while (true)
              {
                auto b = from->read_some(256);
                reactor::wait(this->_transmission);
                to->write(b);
              }
            }
            catch (reactor::network::Exception const&)
            {
              // FIXME: stop listening
            }
          };
        s.run_background(
          elle::sprintf("%s: forward", this),
          std::bind(transmit, socket, target));
        s.run_background(
          elle::sprintf("%s: backward", this),
          std::bind(transmit, target, socket));
      }
    };
  }

  ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, thread);
};

ELLE_TEST_SCHEDULED(
  basics, (Doughnut::OverlayBuilder, builder), (bool, anonymous))
{
  auto keys = infinit::cryptography::rsa::keypair::generate(512);
  auto storage = infinit::storage::Memory::Blocks();
  auto id = infinit::model::Address::random();
  auto make_dht_a = [&]
    {
      return DHT(
        ::id = id,
        ::keys = keys,
        ::storage = elle::make_unique<infinit::storage::Memory>(storage),
        make_overlay = builder);
    };
  auto disk = [&]
    {
      auto dht = make_dht_a();
      auto b = dht.dht->make_block<MutableBlock>(std::string("disk"));
      dht.dht->store(*b, STORE_INSERT);
      return b;
    }();
  auto dht_a = make_dht_a();
  auto before = dht_a.dht->make_block<MutableBlock>(std::string("before"));
  dht_a.dht->store(*before, STORE_INSERT);
  DHT dht_b(
    ::keys = keys, make_overlay = builder, ::storage = nullptr);
  if (anonymous)
    dht_b.dht->overlay()->discover(dht_a.dht->local()->server_endpoints());
  else
    dht_b.dht->overlay()->discover(
      NodeLocation(dht_a.dht->id(), dht_a.dht->local()->server_endpoints()));
  auto after = dht_a.dht->make_block<MutableBlock>(std::string("after"));
  dht_a.dht->store(*after, STORE_INSERT);
  ELLE_LOG("check non-existent block")
    BOOST_CHECK_THROW(
      dht_b.dht->overlay()->lookup(Address::random(), OP_FETCH),
      MissingBlock);
  ELLE_LOG("check block loaded from disk")
    BOOST_CHECK_EQUAL(
      dht_b.dht->overlay()->lookup(disk->address(), OP_FETCH).lock()->id(),
      dht_a.dht->id());
  ELLE_LOG("check block present before connection")
    BOOST_CHECK_EQUAL(
      dht_b.dht->overlay()->lookup(before->address(), OP_FETCH).lock()->id(),
      dht_a.dht->id());
  ELLE_LOG("check block inserted after connection")
    BOOST_CHECK_EQUAL(
      dht_b.dht->overlay()->lookup(after->address(), OP_FETCH).lock()->id(),
      dht_a.dht->id());
}

ELLE_TEST_SCHEDULED(
  dead_peer, (Doughnut::OverlayBuilder, builder), (bool, anonymous))
{
  auto keys = infinit::cryptography::rsa::keypair::generate(512);
  DHT dht_a(::keys = keys, make_overlay = builder, paxos = false);
  elle::With<UTPInstrument>(dht_a.dht->local()->server_endpoints()[0]) <<
    [&] (UTPInstrument& instrument)
    {
    DHT dht_b(::keys = keys, make_overlay = builder, paxos = false);
    infinit::model::Endpoints ep = {
      Endpoint("127.0.0.1", instrument.server.local_endpoint().port()),
    };
    if (anonymous)
      dht_b.dht->overlay()->discover(ep);
    else
      dht_b.dht->overlay()->discover(NodeLocation(dht_a.dht->id(), ep));
    // {
    //   auto block = dht_a.dht->make_block<MutableBlock>(std::string("block"));
    //   ELLE_LOG("store block")
    //     dht_a.dht->store(*block, STORE_INSERT);
    //   ELLE_LOG("lookup block")
    //     BOOST_CHECK_EQUAL(
    //       dht_b.dht->overlay()->lookup(block->address(), OP_FETCH).lock()->id(),
    //       dht_a.dht->id());
    // }
    instrument.transmission().close();
    {
      auto block = dht_a.dht->make_block<MutableBlock>(std::string("block"));
      ELLE_LOG("store block")
        dht_a.dht->store(*block, STORE_INSERT);
    }
  };
}

ELLE_TEST_SUITE()
{
  auto& master = boost::unit_test::framework::master_test_suite();
  auto const kelips_builder =
    [] (Doughnut& dht, std::shared_ptr<Local> local)
    {
      return elle::make_unique<kelips::Node>(
        kelips::Configuration(), local, &dht);
    };
  auto const kouncil_builder =
    [] (Doughnut& dht, std::shared_ptr<Local> local)
    {
      return elle::make_unique<kouncil::Kouncil>(&dht, local);
    };
#define OVERLAY(Name)                                                   \
  auto Name = BOOST_TEST_SUITE(#Name);                                  \
  master.add(Name);                                                     \
  {                                                                     \
    auto basics = std::bind(::basics, Name##_builder, false);           \
    Name->add(BOOST_TEST_CASE(basics), 0, valgrind(5));                 \
    auto basics_anonymous = std::bind(::basics, Name##_builder, true);  \
    Name->add(BOOST_TEST_CASE(basics_anonymous), 0, valgrind(5));       \
    auto dead_peer = std::bind(::dead_peer, Name##_builder, false);     \
    Name->add(BOOST_TEST_CASE(dead_peer), 0, valgrind(5));              \
    auto dead_peer_anonymous = std::bind(::dead_peer,                   \
                                         Name##_builder, true);         \
    Name->add(BOOST_TEST_CASE(dead_peer_anonymous), 0, valgrind(5));    \
  }
  OVERLAY(kelips);
  OVERLAY(kouncil);
#undef OVERLAY
}

// int main()
// {
//   auto const kelips_builder =
//     [] (Doughnut& dht, std::shared_ptr<Local> local)
//     {
//       return elle::make_unique<kelips::Node>(
//         kelips::Configuration(), local, &dht);
//     };
//   dead_peer(kelips_builder, false);
// }
