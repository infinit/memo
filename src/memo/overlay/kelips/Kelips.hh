#pragma once

#include <chrono>
#include <random>

#include <elle/serialization/Serializer.hh>

#include <elle/cryptography/SecretKey.hh>

#include <elle/reactor/Barrier.hh>
#include <elle/reactor/MultiLockBarrier.hh>
#include <elle/reactor/network/rdv-socket.hh>
#include <elle/reactor/network/utp-server.hh>

#include <memo/model/doughnut/Local.hh>
#include <memo/model/doughnut/Remote.hh>
#include <memo/overlay/Overlay.hh>
#include <memo/silo/Silo.hh>

namespace std
{
  template<>
  struct hash<boost::asio::ip::udp::endpoint>
  {
    size_t
    operator ()(boost::asio::ip::udp::endpoint const& e) const;
  };
}

namespace memo
{
  namespace overlay
  {
    namespace kelips
    {
      using Endpoint = model::Endpoint;
      using Endpoints = model::Endpoints;
      using NodeLocation = model::NodeLocation;
      using Address = memo::model::Address;
      using Time = elle::Time;
      using Duration = elle::Duration;

      using TimedEndpoint = std::pair<Endpoint, Time>;
      struct Contact
      {
        // Endpoint we last received a message from
        boost::optional<TimedEndpoint> validated_endpoint;
        // all advertised endpoints
        std::vector<TimedEndpoint> endpoints;
        Address address;
        Duration rtt;
        Time last_gossip; // Last time we gossiped about this contact
        int gossip_count; // Number of times we gossiped about this contact
        // thread performing a contact() call on this node
        elle::reactor::Thread::unique_ptr contacter;
        std::vector<elle::Buffer> pending;
        bool discovered; // was on_discovery signal sent for this contact
        int ping_timeouts; //Number of ping timeouts, resets on any incoming msg
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

      using Files = std::unordered_multimap<Address, File>;
      using Contacts = std::unordered_map<Address, Contact>;
      struct State
      {
        Files files;
        //contacts from all groups. We will allow contacts[_group] to grow more
        std::vector<Contacts> contacts; //contacts from other groups
        Contacts observers;
      };

      namespace packet
      {
        struct Packet;
        struct Pong;
        struct Gossip;
        struct BootstrapRequest;
        struct FileBootstrapRequest;
        struct MultiGetFileRequest;
        struct MultiGetFileReply;
        struct GetFileRequest;
        struct GetFileReply;
        struct PutFileRequest;
        struct PutFileReply;
        struct RequestKey;
      }
      struct PendingRequest;

      struct GossipConfiguration
      {
        GossipConfiguration();
        GossipConfiguration(elle::serialization::SerializerIn& input);
        void
        serialize(elle::serialization::Serializer& s);

        elle::Duration interval;
        /// entry considered new if gossip_count < threshold
        int new_threshold;
        /// entry considered old if last_gossip below that duration
        elle::Duration old_threshold;
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
        using Self = Configuration;
        using Super = overlay::Configuration;

        Configuration();
        Configuration(elle::serialization::SerializerIn& input);
        ELLE_CLONABLE();
        void
        serialize(elle::serialization::Serializer& s) override;
        /// number of groups
        int k = 1;
        /// max number of contacts on each other group
        int max_other_contacts = 6;
        /// query retry
        int query_get_retries = 30;
        /// query retry
        int query_put_retries = 12;
        /// query timeout
        elle::Duration query_timeout = 1s;
        /// query initial ttl
        int query_get_ttl = 10;
        /// query initial ttl
        int query_put_ttl = 10;
        /// query initial ttl
        int query_put_insert_ttl = 3;
        /// entry lifetime before supression
        elle::Duration contact_timeout = 12s;
        /// entry lifetime before supression
        elle::Duration file_timeout = 1200s;
        elle::Duration ping_interval = 1s;
        elle::Duration ping_timeout = 1s;
        /// wait for 'wait' nodes before starting
        int wait = 0;
        bool encrypt = false;
        bool accept_plain = true;
        GossipConfiguration gossip;
        std::unique_ptr<memo::overlay::Overlay>
        make(std::shared_ptr<model::doughnut::Local> server,
             model::doughnut::Doughnut* doughnut) override;
      };

      using SerState = std::pair<
        std::unordered_map<Address, Endpoints>, // contacts
        std::vector<std::pair<Address, Address>> // address, home_node
      >;

      using SerState2 = std::pair<
        std::vector<std::pair<Address, Endpoints>>, // contacts
        std::vector<std::pair<std::string, int>> // delta-blockaddr, owner_index
      >;

      class Node
        : public memo::overlay::Overlay
      {
      public:
        Node(Configuration const& config,
             std::shared_ptr<model::doughnut::Local> local,
             memo::model::doughnut::Doughnut* doughnut);
        ~Node() override;
        void
        start();
        void
        engage();
        /// local hooks interface
        void
        store(memo::model::blocks::Block const& block);
        void
        fetch(Address address,
              std::unique_ptr<memo::model::blocks::Block>& res);
        void
        remove(Address address);
        elle::json::Json
        query(std::string const& k, boost::optional<std::string> const& v) override;

      /*--------.
      | Overlay |
      `--------*/
      protected:
        MemberGenerator
        _allocate(memo::model::Address address, int n) const override;
        MemberGenerator
        _lookup(memo::model::Address address, int n, bool f) const override;
        LocationGenerator
        _lookup(std::vector<memo::model::Address> const& address, int n) const override;
        WeakMember
        _lookup_node(Address address) const override;

      /*-----------.
      | Monitoring |
      `-----------*/
      public:
        std::string
        type_name() const override;
        elle::json::Json
        peer_list() const override;
        elle::json::Json
        stats() const override;

      protected:
        ReachableBlocks
        _compute_reachable_blocks() const override;

      private:
        using Local = memo::model::doughnut::Local;
        using Overlay = memo::overlay::Overlay;
        void
        reload_state(Local& l);
        void
        wait(int contacts);
        void
        send(packet::Packet& p, Contact& c);
        void
        send(packet::Packet& p, Endpoint ep, Address addr);
        void
        send(packet::Packet& p, Contact* c, Endpoint* ep, Address* addr);
        /// consistent address -> group mapper
        int
        group_of(Address const& address) const;
        void
        gossipEmitter();
        void
        pinger();
        /// opportunistic contact grabbing
        void
        onContactSeen(Address addr, Endpoint endpoint, bool observer);
        void
        onPong(packet::Pong*);
        void
        onGossip(packet::Gossip*);
        void
        onBootstrapRequest(packet::BootstrapRequest*);
        void
        onFileBootstrapRequest(packet::FileBootstrapRequest*);
        void
        onGetFileRequest(packet::GetFileRequest*);
        void
        onGetFileReply(packet::GetFileReply*);
        void
        onMultiGetFileRequest(packet::MultiGetFileRequest*);
        void
        onMultiGetFileReply(packet::MultiGetFileReply*);
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
          std::unordered_map<Address, std::vector<TimedEndpoint>>& p);
        void
        cleanup();
        void
        addLocalResults(
          packet::GetFileRequest* p,
          elle::reactor::yielder<NodeLocation> const* yield);
        void
        addLocalResults(
          packet::MultiGetFileRequest* p,
          elle::reactor::yielder<std::pair<Address, NodeLocation>> const* yield,
          std::vector<std::set<Address>>& result_sets);
        void
        kelipsMGet(
          std::vector<Address> files, int n,
          std::function<void (std::pair<Address, NodeLocation>)> yield);
        void
        kelipsGet(Address file, int n, bool local_override, int attempts,
          bool query_node,
          bool fast_mode, // return as soon as we have a result
          std::function<void(NodeLocation)> yield,
          bool ignore_local_cache = false);
        std::vector<NodeLocation>
        kelipsPut(Address file, int n);
        std::unordered_multimap<Address, std::pair<Time, Address>>
        pickFiles();
        std::unordered_map<Address, std::vector<TimedEndpoint>>
        pickContacts();
        std::vector<Address>
        pickOutsideTargets();
        std::vector<Address>
        pickGroupTargets();
        std::pair<elle::cryptography::SecretKey*, bool>
        getKey(Address const& a);
        void
        setKey(Address const& a,
               elle::cryptography::SecretKey sk,
               bool observer);
        void
        process_update(SerState const& s);
        void
        bootstrap(bool use_contacts = true,
                  NodeLocations const& peers = {});
        void
        _discover(NodeLocations const& peers) override;
        bool
        _discovered(model::Address id) override;
        void
        send_bootstrap(NodeLocation const& l);
        SerState
        get_serstate(NodeLocation const& peer);
        // Establish contact with peer and flush buffer.
        void
        contact(Address address);
        void onPacket(elle::ConstWeakBuffer buf, Endpoint source);
        void process(elle::Buffer const& buf, Endpoint source);
        Contact*
        get_or_make(Address address, bool observer,
                    Endpoints const& endpoints, bool make=true);
        Overlay::WeakMember
        make_peer(NodeLocation pl) const;
        packet::RequestKey make_key_request();
        Address _self;
        std::unordered_map<Address, Time> _ping_time;
        std::vector<TimedEndpoint> _local_endpoints;
        /// group we are in
        int _group;
        ELLE_ATTRIBUTE_RX(Configuration, config);
        State _state;
        elle::reactor::network::RDVSocket _gossip;
        elle::reactor::Mutex _udp_send_mutex;
        elle::reactor::Thread::unique_ptr
          _emitter_thread, _pinger_thread,
          _rereplicator_thread;
        std::unordered_map<int, std::shared_ptr<PendingRequest>>
          _pending_requests;
        /// Addresses for which we accepted a put.
        std::vector<Address> _promised_files;
        // address -> (isObserver, key)
        std::unordered_map<Address,
          std::pair<elle::cryptography::SecretKey, bool>> _keys;
        /// Bootstrap pending auth.
        Endpoints _pending_bootstrap_endpoints;
        std::vector<Address> _pending_bootstrap_address;
        std::vector<Address> _bootstrap_requests_sent;
        int _next_id;
        int _port;
        bool _observer;
        int _dropped_puts;
        int _dropped_gets;
        int _failed_puts;
        using Index = std::unordered_map<Address, int>;
        Index _under_duplicated;
        std::unordered_map<std::string, elle::Buffer> _challenges;
        ELLE_ATTRIBUTE(
          (std::unordered_map<Address, Overlay::Member>),
          peer_cache);
        mutable
        std::unordered_map<Address,
          std::pair<elle::reactor::Thread::unique_ptr, bool>> _node_lookups;
        std::unordered_map<elle::reactor::Thread*, elle::reactor::Thread::unique_ptr>
          _bootstraper_threads;
        elle::reactor::MultiLockBarrier _in_use;
        bool _terminating;
      };
    }
  }
}

#include <memo/overlay/kelips/Kelips.hxx>
