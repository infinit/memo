#include <elle/test.hh>

#include <elle/protocol/ChanneledStream.hh>
#include <elle/protocol/Serializer.hh>

#include <infinit/RPC.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("byzantine")

ELLE_TEST_SCHEDULED(unknown_rpc)
{
  ELLE_LOG("creating dht");
  auto dht = DHT{};
  {
    ELLE_LOG("connecting");
    auto s = dht.connect_tcp();
    auto elle_version = infinit::elle_serialization_version(dht.dht->version());
    elle::protocol::Serializer ser(s, elle_version, false);
    auto&& channels = elle::protocol::ChanneledStream{ser};
    auto rpc = infinit::RPC<void()>("doom_is_coming", channels, dht.dht->version());
    BOOST_CHECK_THROW(rpc(), infinit::UnknownRPC);
  }

  ELLE_LOG("creating dht_b");
  auto dht_b = DHT(keys = dht.dht->keys());
  auto peer = dht_b.dht->dock().make_peer(
    infinit::model::NodeLocation(dht.dht->id(),
                                 dht.dht->local()->server_endpoints())).lock();
  auto& r = dynamic_cast<infinit::model::doughnut::Remote&>(*peer);
  ELLE_LOG("connecting");
  r.connect();
  // By default Local::broadcast ignores unknown RPCs.
  dht.dht->local()->broadcast<void>("doom_is_backfiring");
}

ELLE_TEST_SUITE()
{
  // It takes 10sec _with_ Valgrind on a laptop in Docker, otherwise
  // less than a second.
  auto timeout = valgrind(5, 5);
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(unknown_rpc), 0, timeout);
}
