#ifndef INFINIT_OVERLAY_KELIPS_HH
# define INFINIT_OVERLAY_KELIPS_HH

# include <infinit/overlay/Overlay.hh>
# include <reactor/network/udp-socket.hh>
# include <reactor/Barrier.hh>
# include <elle/serialization/Serializer.hh>

#include <infinit/model/doughnut/Local.hh>
#include <infinit/storage/Storage.hh>

#include <random>

namespace kelips
{
  typedef boost::asio::ip::tcp::endpoint RpcEndpoint;
  typedef boost::asio::ip::udp::endpoint GossipEndpoint;
  struct PrettyGossipEndpoint: public GossipEndpoint
  {
    using GossipEndpoint::GossipEndpoint;
  };
  typedef infinit::model::Address Address;
  typedef std::chrono::time_point<std::chrono::steady_clock> Time;
  typedef Time::duration Duration;
  //typedef std::chrono::duration<long, std::ratio<1, 1000000>> Duration;
  struct Contact
  {
    GossipEndpoint endpoint;
    Address address;
    Duration rtt;
    Time last_seen;
    Time last_gossip;
    int gossip_count;
  };
  struct File
  {
    Address address;
    Address home_node;
    Time last_seen;
    Time last_gossip;
    int gossip_count;
  };
  typedef std::unordered_multimap<Address, File> Files;
  typedef std::unordered_map<Address, Contact> Contacts;
  struct State
  {
    Files files;
     //contacts from all groups. We will allow contacts[_group] to grow more
    std::vector<Contacts> contacts; //contacts from other groups
  };

  struct GossipConfiguration
  {
    GossipConfiguration();
    GossipConfiguration(elle::serialization::SerializerIn& input) {serialize(input);}
    void
    serialize(elle::serialization::Serializer& s)
    {
      s.serialize("interval_ms", interval_ms);
      s.serialize("new_threshold", new_threshold);
      s.serialize("old_threshold_ms", old_threshold_ms);
      s.serialize("files", files);
      s.serialize("contacts_group", contacts_group);
      s.serialize("contacts_other", contacts_other);
      s.serialize("group_target", group_target);
      s.serialize("other_target", other_target);
      s.serialize("bootstrap_group_target", bootstrap_group_target);
      s.serialize("bootstrap_other_target", bootstrap_other_target);
    }
    int interval_ms;
    int new_threshold; // entry considered new if gossip_count < threshold
    int old_threshold_ms; // entry considered old if last_gossip below that duration
    int files; //how many files per packet
    int contacts_group; // how many nodes in other groups per packet
    int contacts_other; // how many nodes in self group per packet
    int group_target; // how many gossip targets in group
    int other_target; // how many gossip targets in other groups
    int bootstrap_group_target;
    int bootstrap_other_target;
  };

  struct Configuration
  {
    Configuration();
    Configuration(elle::serialization::SerializerIn& input) {serialize(input);}
    void
    serialize(elle::serialization::Serializer& s);
    Address node_id;
    int k; // number of groups
    int max_other_contacts; // max number of contacts on each other group
    int query_get_retries;    // query retry
    int query_put_retries;    // query retry
    int query_timeout_ms; // query timeout
    int query_get_ttl; // query initial ttl
    int query_put_ttl; // query initial ttl
    int query_put_insert_ttl; // query initial ttl
    int contact_timeout_ms; // entry lifetime before supression
    int file_timeout_ms; // entry lifetime before supression
    int ping_interval_ms;
    int ping_timeout_ms;
    std::vector<PrettyGossipEndpoint> bootstrap_nodes;
    int wait; // wait for 'wait' nodes before starting
    GossipConfiguration gossip;
  };

  namespace packet
  {
    struct Packet;
    struct Pong;
    struct Gossip;
    struct BootstrapRequest;
    struct GetFileRequest;
    struct GetFileReply;
    struct PutFileRequest;
    struct PutFileReply;
  }
  struct PendingRequest;
  class Node
    : public infinit::overlay::Overlay
    , public elle::Printable
  {
  public:
    Node(Configuration const& config, bool observer);
    void start();
    void engage();
    void
    register_local(
      std::shared_ptr<infinit::model::doughnut::Local> local) override;
    std::vector<RpcEndpoint> address(Address file,
                                     infinit::overlay::Operation op,
                                     int n);
    void print(std::ostream& stream) const override;
    // local hooks interface
    void store(infinit::model::blocks::Block const& block, infinit::model::StoreMode mode);
    void fetch(Address address, std::unique_ptr<infinit::model::blocks::Block>& res);
    void remove(Address address);
    // overlay
  protected:
    virtual Overlay::Members _lookup(infinit::model::Address address, int n, infinit::overlay::Operation op) const override;
  private:
    typedef infinit::model::doughnut::Local Local;
    typedef infinit::overlay::Overlay Overlay;
    void reload_state(Local& l);
    void wait(int contacts);
    void send(elle::Buffer const& data, GossipEndpoint endpoint);
    int group_of(Address const& address); // consistent address -> group mapper
    void gossipListener();
    void gossipEmitter();
    void pinger();
    // opportunistic contact grabbing
    void onContactSeen(Address addr, GossipEndpoint endpoint);
    void onPong(packet::Pong*);
    void onGossip(packet::Gossip*);
    void onBootstrapRequest(packet::BootstrapRequest*);
    void onGetFileRequest(packet::GetFileRequest*);
    void onGetFileReply(packet::GetFileReply*);
    void onPutFileRequest(packet::PutFileRequest*);
    void onPutFileReply(packet::PutFileReply*);
    void filterAndInsert(std::vector<Address> files, int target_count,
                        std::unordered_map<Address, std::pair<Time, Address>>& p);
    void filterAndInsert(std::vector<Address> files, int target_count, int group,
                        std::unordered_map<Address, std::pair<Time, GossipEndpoint>>& p);
    void cleanup();
    void addLocalResults(packet::GetFileRequest* p);
    std::vector<RpcEndpoint> kelipsGet(Address file, int n);
    std::vector<RpcEndpoint> kelipsPut(Address file, int n);
    std::unordered_multimap<Address, std::pair<Time, Address>> pickFiles();
    std::unordered_map<Address, std::pair<Time, GossipEndpoint>> pickContacts();
    std::vector<GossipEndpoint> pickOutsideTargets();
    std::vector<GossipEndpoint> pickGroupTargets();
    Address _ping_target;
    Time _ping_time;
    reactor::Barrier _ping_barrier;
    Address _self;
    GossipEndpoint _local_endpoint;
    int _group; // group we are in
    Configuration _config;
    State _state;
    reactor::network::UDPSocket _gossip;
    reactor::Mutex _udp_send_mutex;
    std::unique_ptr<reactor::Thread> _emitter_thread, _listener_thread, _pinger_thread;
    std::default_random_engine _gen;
    std::unordered_map<int, std::shared_ptr<PendingRequest>> _pending_requests;
    std::vector<Address> _promised_files; // addresses for which we accepted a put
    int _next_id;
    int _port;
    bool _observer;
  };
}


namespace infinit
{
  namespace overlay
  {
    namespace kelips
    {
      struct Configuration
        : public overlay::Configuration
      {
        Configuration();
        Configuration(elle::serialization::SerializerIn& input);

        void
        serialize(elle::serialization::Serializer& s);

        virtual
        std::unique_ptr<infinit::overlay::Overlay>
        make(std::vector<std::string> const& hosts, bool server) override;

        ::kelips::Configuration config;
      };
    }
  }
}

namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<kelips::PrettyGossipEndpoint>
    {
      typedef std::string Type;
      static std::string convert(kelips::PrettyGossipEndpoint& ep)
      {
        return ep.address().to_string() + ":" + std::to_string(ep.port());
      }
      static kelips::PrettyGossipEndpoint convert(std::string const& repr)
      {
        size_t sep = repr.find_first_of(':');
        auto addr = boost::asio::ip::address::from_string(repr.substr(0, sep));
        int port = std::stoi(repr.substr(sep + 1));
        return kelips::PrettyGossipEndpoint(addr, port);
      }
    };
  }
}

/* TODO:
- overlay integration
*/

#endif
