#ifndef INFINIT_OVERLAY_KELIPS_HH
# define INFINIT_OVERLAY_KELIPS_HH

# include <infinit/overlay/Overlay.hh>
# include <reactor/network/udp-socket.hh>
# include <reactor/Barrier.hh>
# include <reactor/Generator.hh>
# include <elle/serialization/Serializer.hh>

#include <infinit/model/doughnut/Local.hh>
#include <infinit/storage/Storage.hh>

#include <cryptography/SecretKey.hh>
#include <random>
#include <chrono>

namespace std
{
  template<>
  struct hash<boost::asio::ip::udp::endpoint>
  {
    size_t
    operator ()(boost::asio::ip::udp::endpoint const& e) const;
  };
}

namespace infinit
{
  namespace overlay
  {
    namespace kelips
    {
      typedef boost::asio::ip::tcp::endpoint RpcEndpoint;
      typedef boost::asio::ip::udp::endpoint GossipEndpoint;
      typedef infinit::model::Address Address;
      typedef std::chrono::time_point<std::chrono::system_clock> Time;
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

      std::ostream&
      operator << (std::ostream& output, Contact const& contact);

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

      struct GossipConfiguration
      {
        GossipConfiguration();
        GossipConfiguration(elle::serialization::SerializerIn& input);
        void
        serialize(elle::serialization::Serializer& s);

        int interval_ms;
        /// entry considered new if gossip_count < threshold
        int new_threshold;
        /// entry considered old if last_gossip below that duration
        int old_threshold_ms;
        ///how many files per packet
        int files;
        /// how many nodes in other groups per packet
        int contacts_group;
        /// how many nodes in self group per packet
        int contacts_other;
        /// how many gossip targets in group
        int group_target;
        /// how many gossip targets in other groups
        int other_target;
        int bootstrap_group_target;
        int bootstrap_other_target;
      };

      struct Configuration
        : public overlay::Configuration
      {
        Configuration();
        Configuration(elle::serialization::SerializerIn& input);
        void
        serialize(elle::serialization::Serializer& s);
        /// number of groups
        int k;
        /// max number of contacts on each other group
        int max_other_contacts;
        /// query retry
        int query_get_retries;
        /// query retry
        int query_put_retries;
        /// query timeout
        int query_timeout_ms;
        /// query initial ttl
        int query_get_ttl;
        /// query initial ttl
        int query_put_ttl;
        /// query initial ttl
        int query_put_insert_ttl;
        /// entry lifetime before supression
        int contact_timeout_ms;
        /// entry lifetime before supression
        int file_timeout_ms;
        int ping_interval_ms;
        int ping_timeout_ms;
        std::vector<GossipEndpoint> bootstrap_nodes;
        /// wait for 'wait' nodes before starting
        int wait;
        bool encrypt;
        bool accept_plain;
        infinit::model::doughnut::Local::Protocol rpc_protocol;
        GossipConfiguration gossip;
        virtual
        std::unique_ptr<infinit::overlay::Overlay>
        make(std::vector<std::string> const& hosts, bool server,
             model::doughnut::Doughnut* doughnut) override;
      };

      class Node
        : public infinit::overlay::Overlay
        , public elle::Printable
      {
      public:
        Node(Configuration const& config,
             bool observer,
             elle::UUID node_id,
             infinit::model::doughnut::Doughnut* doughnut);
        virtual ~Node();
        void
        start();
        void
        engage();
        void
        register_local(
          std::shared_ptr<infinit::model::doughnut::Local> local) override;
         reactor::Generator<RpcEndpoint>
         address(Address file,
                 infinit::overlay::Operation op,
                 int n);
        void
        print(std::ostream& stream) const override;
        /// local hooks interface
        void
        store(infinit::model::blocks::Block const& block,
              infinit::model::StoreMode mode);
        void
        fetch(Address address,
              std::unique_ptr<infinit::model::blocks::Block>& res);
        void
        remove(Address address);
        elle::json::Json
        query(std::string const& k, boost::optional<std::string> const& v) override;

      /*--------.
      | Overlay |
      `--------*/
      protected:
        virtual
        reactor::Generator<Overlay::Member>
        _lookup(infinit::model::Address address, int n,
                infinit::overlay::Operation op) const override;

      private:
        typedef infinit::model::doughnut::Local Local;
        typedef infinit::overlay::Overlay Overlay;
        void
        reload_state(Local& l);
        void
        wait(int contacts);
        void
        send(packet::Packet& p, GossipEndpoint e, Address a);
        /// consistent address -> group mapper
        int
        group_of(Address const& address);
        void
        gossipListener();
        void
        gossipEmitter();
        void
        pinger();
        /// opportunistic contact grabbing
        void
        onContactSeen(Address addr, GossipEndpoint endpoint);
        void
        onPong(packet::Pong*);
        void
        onGossip(packet::Gossip*);
        void
        onBootstrapRequest(packet::BootstrapRequest*);
        void
        onGetFileRequest(packet::GetFileRequest*);
        void
        onGetFileReply(packet::GetFileReply*);
        void
        onPutFileRequest(packet::PutFileRequest*);
        void
        onPutFileReply(packet::PutFileReply*);
        void
        filterAndInsert(
          std::vector<Address> files, int target_count,
          std::unordered_map<Address, std::pair<Time, Address>>& p);
        void
        filterAndInsert(
          std::vector<Address> files, int target_count, int group,
          std::unordered_map<Address, std::pair<Time, GossipEndpoint>>& p);
        void
        cleanup();
        void
        addLocalResults(packet::GetFileRequest* p, reactor::yielder<RpcEndpoint>::type const* yield);
        reactor::Generator<RpcEndpoint>
        kelipsGet(Address file, int n, bool local_override = false);
        std::vector<RpcEndpoint>
        kelipsPut(Address file, int n);
        std::unordered_multimap<Address, std::pair<Time, Address>>
        pickFiles();
        std::unordered_map<Address, std::pair<Time, GossipEndpoint>>
        pickContacts();
        std::vector<std::pair<GossipEndpoint, Address>>
        pickOutsideTargets();
        std::vector<std::pair<GossipEndpoint, Address>> \
        pickGroupTargets();
        infinit::cryptography::SecretKey*
        getKey(Address const& a, GossipEndpoint const& e);
        void
        setKey(Address const& a, GossipEndpoint const& e,
               infinit::cryptography::SecretKey sk);
        Address _self;
        Address _ping_target;
        Time _ping_time;
        reactor::Barrier _ping_barrier;
        GossipEndpoint _local_endpoint;
        /// group we are in
        int _group;
        Configuration _config;
        State _state;
        reactor::network::UDPSocket _gossip;
        reactor::Mutex _udp_send_mutex;
        std::unique_ptr<reactor::Thread>
          _emitter_thread, _listener_thread, _pinger_thread;
        std::default_random_engine _gen;
        std::unordered_map<int, std::shared_ptr<PendingRequest>>
          _pending_requests;
        /// Addresses for which we accepted a put.
        std::vector<Address> _promised_files;
        std::unordered_map<Address, infinit::cryptography::SecretKey> _keys;
        std::unordered_map<GossipEndpoint, infinit::cryptography::SecretKey>
          _observer_keys;
        /// Bootstrap pending auth.
        std::vector<GossipEndpoint> _pending_bootstrap;
        reactor::network::UTPServer _remotes_server;
        std::shared_ptr<infinit::model::doughnut::Local> _local;
        /// Whether we've seen someone from our group.
        reactor::Barrier _bootstraping;
        int _next_id;
        int _port;
        bool _observer;
        int _dropped_puts;
        int _dropped_gets;
        int _failed_puts;
      };
    }
  }
}

# include <infinit/overlay/kelips/Kelips.hxx>

#endif
