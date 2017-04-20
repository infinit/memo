#pragma once

#include <elle/reactor/Barrier.hh>
#include <elle/reactor/network/udp-socket.hh>
#include <elle/serialization/Serializer.hh>

#include <infinit/model/doughnut/Local.hh>
#include <infinit/overlay/Overlay.hh>

namespace kademlia
{
  using Endpoint = boost::asio::ip::udp::endpoint;
  using Address = infinit::model::Address;
  using Time = std::chrono::time_point<std::chrono::steady_clock>;
  using Duration = Time::duration;
  struct PrettyEndpoint: public Endpoint
  {
    PrettyEndpoint() {}
    PrettyEndpoint(boost::asio::ip::address h, int p)
    : Endpoint(h, p) {}
  };

  struct Configuration
  {
    Configuration();
    Configuration(elle::serialization::SerializerIn& input) {serialize(input);}
    void
    serialize(elle::serialization::Serializer& s);
    int port;
    int wait;
    int wait_ms;
    int address_size; // in bits
    int k; // number of entries to keep/bucket, and response size
    int alpha; // number of concurrent requests to send
    int ping_interval_ms;
    int refresh_interval_ms;
    int storage_lifetime_ms;
  };

  namespace packet
  {
    struct Ping;
    struct Pong;
    struct FindNode;
    struct FindNodeReply;
    struct FindValue;
    struct FindValueReply;
    struct Store;
  }

  class Kademlia
    : public infinit::overlay::Overlay
    , public elle::Printable
  {
  public:
    Kademlia(Configuration const& config,
             std::shared_ptr<infinit::model::doughnut::Local> server,
             infinit::model::doughnut::Doughnut* doughnut);
    virtual ~Kademlia();
    void store(infinit::model::blocks::Block const& block);
    void remove(Address address);
    void fetch(Address address, std::unique_ptr<infinit::model::blocks::Block> & b);
    void print(std::ostream& stream) const override;

  /*------.
  | Peers |
  `------*/
  protected:
    void
    _discover(infinit::overlay::NodeLocations const& peers) override;
    MemberGenerator
    _allocate(infinit::model::Address address,
            int n) const override;
    MemberGenerator
    _lookup(infinit::model::Address address,
            int n,
            bool fast) const override;
    Overlay::WeakMember
    _lookup_node(infinit::model::Address address) const override;

  /*-----------.
  | Monitoring |
  `-----------*/
  public:
    std::string
    type_name() const override;
    elle::json::Array
    peer_list() const override;
    elle::json::Object
    stats() override;

  private:
    void _loop();
    void _ping();
    void _refresh();
    void _reload(infinit::model::doughnut::Local& l);
    void _bootstrap();
    void _cleanup();
    void _republish();
    void send(elle::Buffer const& data, Endpoint endpoint);
    std::unordered_map<Address, Endpoint> closest(Address addr);

    void onPong(packet::Pong*);
    void onFindValue(packet::FindValue*);
    void onFindValueReply(packet::FindValueReply*);
    void onFindNode(packet::FindNode*);
    void onFindNodeReply(packet::FindNodeReply*);
    void onStore(packet::Store*);
    void onContactSeen(Address sender, Endpoint ep);
    Address dist(Address const& a, Address const& b);
    bool less(Address const& a, Address const& b);
    bool more(Address const& a, Address const& b);
    int bucket_of(Address const&);
    using Local = infinit::model::doughnut::Local;
    using Overlay = infinit::overlay::Overlay;
    std::unique_ptr<elle::reactor::Thread> _looper;
    std::unique_ptr<elle::reactor::Thread> _pinger;
    std::unique_ptr<elle::reactor::Thread> _refresher;
    std::unique_ptr<elle::reactor::Thread> _cleaner;
    std::unique_ptr<elle::reactor::Thread> _republisher;
    elle::reactor::network::UDPSocket _socket;
    elle::reactor::Mutex _udp_send_mutex;
    Configuration _config;
    Address _mask;
    Address _self;

    struct Node
    {
      Address address;
      Endpoint endpoint;
      Time last_seen;
      int unack_ping;
    };

    struct Store
    {
      Endpoint endpoint;
      Time last_seen;
    };

    struct Query
    {
      Address target;
      std::vector<Address> res;
      std::vector<Address> candidates;
      std::unordered_map<Address, Endpoint> endpoints;
      int pending; // number of requests in flight
      std::vector<Address> queried;
      elle::reactor::Barrier barrier;
      std::vector<Endpoint> storeResult;
      int n; // number ofr results requested
      int steps; // number of replies we got
    };

    std::shared_ptr<Query> startQuery(Address const& a, bool storage);
    boost::optional<Address> recurseRequest(
      Query& q,
      std::unordered_map<Address, Endpoint> const& nodes);
    void finish(int rid, Query&q);
    Endpoint _local_endpoint;
    std::vector<std::vector<Node>> _routes;
    std::unordered_map<Address, std::vector<Store>> _storage;
    std::unordered_map<int, std::shared_ptr<Query>> _queries;
    int _port;
  };
}

namespace infinit
{
  namespace overlay
  {
    namespace kademlia
    {
      struct Configuration
        : public overlay::Configuration
      {
        using Self = infinit::overlay::kademlia::Configuration;
        using Super = overlay::Configuration;
        Configuration();
        Configuration(elle::serialization::SerializerIn& input);
        ELLE_CLONABLE();
        void
        serialize(elle::serialization::Serializer& s) override;
        virtual
        std::unique_ptr<infinit::overlay::Overlay>
        make(std::shared_ptr<infinit::model::doughnut::Local> local,
             model::doughnut::Doughnut* doughnut) override;
        ::kademlia::Configuration config;
      };
    }
  }
}
