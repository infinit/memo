#pragma once

#include <unordered_map>

#include <elle/Clonable.hh>
#include <elle/json/json.hh>
#include <elle/log.hh>

#include <elle/reactor/network/tcp-socket.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/Generator.hh>
#include <elle/reactor/Thread.hh>

#include <memo/model/Address.hh>
#include <memo/model/Endpoints.hh>
#include <memo/model/doughnut/fwd.hh>
#include <memo/model/doughnut/protocol.hh>
#include <memo/model/prometheus.hh>
#include <memo/serialization.hh>

namespace prometheus
{
  class Gauge;
}

namespace memo
{
  namespace overlay
  {
    using model::Endpoints;
    using model::NodeLocation;
    using model::NodeLocations;

    class Overlay
      : public elle::Printable
    {
    /*------.
    | Types |
    `------*/
    public:
      /// Remote or local peer.
      using Member = std::shared_ptr<model::doughnut::Peer>;
      /// Members with weak or strong ownerships.
      using WeakMember = std::ambivalent_ptr<model::doughnut::Peer>;
      /// Collection of members.
      using Members = std::vector<Member>;

    /*-------------.
    | Construction |
    `-------------*/
    public:
      /// Construct an Overlay.
      ///
      /// @arg dht   The owning Doughnut.
      /// @arg local The optional Local.
      Overlay(model::doughnut::Doughnut* dht,
              std::shared_ptr<memo::model::doughnut::Local> local);
      Overlay() = delete;
      Overlay(Overlay const&) = delete;
      Overlay(Overlay&&) = delete;
      Overlay& operator= (Overlay const&) = delete;
      Overlay& operator= (Overlay&&) = delete;
      /// Destroy an Overlay.
      virtual
      ~Overlay();
      /// Prepare for destruction.
      void
      cleanup();
      /// The owning Doughnut.
      ELLE_ATTRIBUTE_R(model::doughnut::Doughnut*, doughnut);
      /// This node's id.
      ELLE_attribute_r(model::Address, id);
      /// The node's optional Local.
      ELLE_ATTRIBUTE_R(std::shared_ptr<model::doughnut::Local>, local);

    protected:
      virtual
      void
      _cleanup();

    /*-----------.
    | Properties |
    `-----------*/
    public:
      /// Whether we accept new blocks.
      ELLE_ATTRIBUTE_Rw(bool, storing, virtual);
    protected:
      virtual
      void
      _store(bool value);

    /*------.
    | Peers |
    `------*/
    public:
      /// Discover one anonymous peer.
      void
      discover(Endpoints const& peer);
      /// Discover anonymous peers.
      void
      discover(std::vector<Endpoints> const& peers);
      /// Discover one peer.
      void
      discover(NodeLocation const& peer);
      /// Discover peers.
      void
      discover(NodeLocations const& peers);
      bool
      discovered(model::Address id);
    protected:
      virtual
      void
      _discover(NodeLocations const& peers) = 0;
      virtual
      bool
      _discovered(model::Address id) = 0;

    /*------.
    | Hooks |
    `------*/
    public:
      /// Announcing discovered/connected nodes.
      using DiscoveryEvent =
        boost::signals2::signal<auto (NodeLocation id, bool observer) -> void>;
      ELLE_ATTRIBUTE_RX(DiscoveryEvent, on_discovery);
      /// Announcing disappeared/disconnected nodes.
      using DisappearanceEvent =
        boost::signals2::signal<auto (model::Address id, bool observer) -> void>;
      ELLE_ATTRIBUTE_RX(DisappearanceEvent, on_disappearance);

    /*-------.
    | Lookup |
    `-------*/
    public:
      /// A generator of members.
      using MemberGenerator = elle::reactor::Generator<WeakMember>;

      /// A generator of addresses, i.e., for each address, a provider
      using LocationGenerator
        = elle::reactor::Generator<std::pair<model::Address, WeakMember>>;

      /// Find owner for a new block
      ///
      /// @arg address  Address of the blocks to place
      /// @arg n        How many owners to look for
      MemberGenerator
      allocate(model::Address address, int n) const;
      /// Lookup multiple addresses (OP_FETCH/UPDATE only)
      LocationGenerator
      lookup(std::vector<model::Address> const& addresses, int n) const;
      /// Lookup blocks
      ///
      /// @arg address  Address of the blocks to search
      /// @arg n        How many owners to look for
      /// @arg fast     Whether to prefer a faster, partial answer.
      MemberGenerator
      lookup(model::Address address, int n, bool fast = false) const;
      /// Lookup a single block owner
      WeakMember
      lookup(model::Address address) const;
      /// Lookup a node from its id.
      ///
      /// @arg id Id of the node to lookup.
      /// @raise elle::Error if the node is not found.
      WeakMember
      lookup_node(model::Address id) const;
      /// Lookup nodes from their ids.
      ///
      /// @arg ids ids of the nodes to lookup.
      /// @raise elle::Error if the node is not found.
      MemberGenerator
      lookup_nodes(std::unordered_set<model::Address> ids) const;
    protected:
      virtual
      MemberGenerator
      _allocate(model::Address address, int n) const = 0;
      virtual
      LocationGenerator
      _lookup(std::vector<model::Address> const& addresses, int n) const;
      virtual
      MemberGenerator
      _lookup(model::Address address, int n, bool fast) const = 0;
      /// Lookup a node by id
      ///
      /// @raise elle::Error if the node cannot be found.
      virtual
      WeakMember
      _lookup_node(model::Address address) const = 0;

    /*------.
    | Query |
    `------*/
    public:
      /// Query overlay specific informations.
      virtual
      elle::json::Json
      query(std::string const& k, boost::optional<std::string> const& v);

    /*-----------.
    | Monitoring |
    `-----------*/
    public:
      virtual
      std::string
      type_name() const = 0;
      virtual
      elle::json::Array
      peer_list() const = 0;
      virtual
      elle::json::Object
      stats() const = 0;

#if MEMO_ENABLE_PROMETHEUS
      /// Gauge on the number of accessible blocks.
      ELLE_ATTRIBUTE_R(prometheus::GaugePtr, reachable_blocks_gauge);
      ELLE_ATTRIBUTE_R(prometheus::GaugePtr, reachable_mutable_blocks_gauge);
      ELLE_ATTRIBUTE_R(prometheus::GaugePtr, reachable_immutable_blocks_gauge);
      ELLE_ATTRIBUTE_R(prometheus::GaugePtr,
        underreplicated_immutable_blocks_gauge);
      ELLE_ATTRIBUTE_R(prometheus::GaugePtr,
        overreplicated_immutable_blocks_gauge);
      ELLE_ATTRIBUTE_R(prometheus::GaugePtr,
        underreplicated_mutable_blocks_gauge);
      ELLE_ATTRIBUTE_R(prometheus::GaugePtr, under_quorum_mutable_blocks_gauge);
#endif

    public:
      /// Information about reachable blocks
      struct ReachableBlocks
      {
        /// Total number of blocks we know about
        int total_blocks;
        int mutable_blocks;
        int immutable_blocks;
        int underreplicated_immutable_blocks;
        int overreplicated_immutable_blocks;
        int underreplicated_mutable_blocks;
        int under_quorum_mutable_blocks;
        std::vector<model::Address> sample_underreplicated;
      };

    protected:
      /// Overlay-dependant computation of how many blocks are reachable.
      virtual
      ReachableBlocks
      _compute_reachable_blocks() const;
      /// Request for reachable_blocks to be updated.
      /// Call from overlay when something changes.
      void
      _update_reachable_blocks();
      ELLE_ATTRIBUTE_RW(elle::Duration, reachable_max_update_period);
      ELLE_ATTRIBUTE(elle::reactor::Barrier, reachable_blocks_barrier);
      ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr,
                     reachable_blocks_thread);
      ELLE_ATTRIBUTE_R(ReachableBlocks, reachable_blocks);
      ELLE_ATTRIBUTE_RX(elle::reactor::Signal, reachable_blocks_updated);
      void
      _reachable_blocks_loop();
    /*----------.
    | Printable |
    `----------*/
    public:
      void
      print(std::ostream& o) const override;
    };

    struct Configuration
      : public elle::serialization::VirtuallySerializable<Configuration, false>
      , public elle::Clonable<Configuration>
    {
      model::doughnut::Protocol rpc_protocol;

      Configuration() = default;
      Configuration(elle::serialization::SerializerIn& input);
      static constexpr char const* virtually_serializable_key = "type";
      /// Perform any initialization required at join time.
      // virtual
      // void
      // join();
      void
      serialize(elle::serialization::Serializer& s) override;
      using serialization_tag = memo::serialization_tag;
      virtual
      std::unique_ptr<memo::overlay::Overlay>
      make(std::shared_ptr<model::doughnut::Local> local,
           model::doughnut::Doughnut* doughnut) = 0;
    };

    /*-----------.
    | Exceptions |
    `-----------*/

    class NodeNotFound
      : public elle::Error
    {
    public:
      NodeNotFound(model::Address id);
      ELLE_ATTRIBUTE_R(model::Address, id);
    };
  }
}
