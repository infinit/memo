#include <elle/test.hh>

#include <protocol/Serializer.hh>
#include <protocol/ChanneledStream.hh>

#include <infinit/RPC.hh>

#include "DHT.hh"

ELLE_LOG_COMPONENT("byzantine")

ELLE_TEST_SCHEDULED(unknown_rpc)
{
  DHT dht;
  {
    auto s = dht.connect_tcp();
    auto elle_version = infinit::elle_serialization_version(dht.dht->version());
    infinit::protocol::Serializer ser(s, elle_version, false);
    infinit::protocol::ChanneledStream channels(ser, elle_version);
    infinit::RPC<void()> rpc("doom_is_coming", channels, dht.dht->version());
    BOOST_CHECK_THROW(rpc(), infinit::UnknownRPC);
  }
  DHT dht_b(keys = dht.dht->keys());
  infinit::model::doughnut::Remote r(
    *dht_b.dht, dht.dht->id(), dht.dht->local()->server_endpoints(),
    boost::none, boost::none, infinit::model::doughnut::Protocol::tcp);
  r.connect();
  // By default Local::broadcast ignores unknown RPCs.
  dht.dht->local()->broadcast<void>("doom_is_backfiring");
}

ELLE_TEST_SUITE()
{
  auto& suite = boost::unit_test::framework::master_test_suite();
  suite.add(BOOST_TEST_CASE(unknown_rpc));
}
