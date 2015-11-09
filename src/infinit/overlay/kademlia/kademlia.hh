#ifndef INFINIT_OVERLAY_KADEMLIA_HH
# define INFINIT_OVERLAY_KADEMLIA_HH


# include <infinit/overlay/Overlay.hh>
# include <elle/serialization/Serializer.hh>
# include <reactor/network/udp-socket.hh>
# include <reactor/Barrier.hh>
# include <reactor/Generator.hh>
# include <infinit/model/doughnut/Local.hh>

namespace kademlia
{
  typedef boost::asio::ip::udp::endpoint Endpoint;
  typedef infinit::model::Address Address;
  typedef std::chrono::time_point<std::chrono::steady_clock> Time;
  typedef Time::duration Duration;
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
    Address node_id;
    int port;
    std::vector<PrettyEndpoint> bootstrap_nodes;
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
    Kademlia(infinit::model::Address node_id, Configuration const& config, bool server,
      infinit::model::doughnut::Doughnut* doughnut);
    virtual ~Kademlia();
    void store(infinit::model::blocks::Block const& block, infinit::model::StoreMode mode);
    void remove(Address address);
    void fetch(Address address, std::unique_ptr<infinit::model::blocks::Block> & b);
    void print(std::ostream& stream) const override;
    void
    register_local(
      std::shared_ptr<infinit::model::doughnut::Local> local) override;
  protected:
    virtual reactor::Generator<Member> _lookup(infinit::model::Address address,
                                     int n, infinit::overlay::Operation op)
                                  const override;
    virtual Overlay::Member _lookup_node(infinit::model::Address address) override;
  public:

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
    typedef infinit::model::doughnut::Local Local;
    typedef infinit::overlay::Overlay Overlay;
    std::unique_ptr<reactor::Thread> _looper;
    std::unique_ptr<reactor::Thread> _pinger;
    std::unique_ptr<reactor::Thread> _refresher;
    std::unique_ptr<reactor::Thread> _cleaner;
    std::unique_ptr<reactor::Thread> _republisher;
    reactor::network::UDPSocket _socket;
    reactor::Mutex _udp_send_mutex;
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
      reactor::Barrier barrier;
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
        Configuration();
        Configuration(elle::serialization::SerializerIn& input);
        void
        serialize(elle::serialization::Serializer& s) override;
        virtual
        std::unique_ptr<infinit::overlay::Overlay>
        make(model::Address id,
             NodeEndpoints const& hosts, bool server,
             model::doughnut::Doughnut* doughnut) override;
        ::kademlia::Configuration config;
      };
    }
  }
}

#endif
