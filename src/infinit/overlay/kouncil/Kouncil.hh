#pragma once

#include <random>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <elle/athena/LamportAge.hh>
#include <elle/optional.hh>
#include <elle/multi_index_container.hh>
#include <elle/unordered_map.hh>

#include <infinit/model/doughnut/Peer.hh>
#include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace symbols
  {
    ELLE_DAS_SYMBOL(disappearance);
    ELLE_DAS_SYMBOL(endpoints);
    ELLE_DAS_SYMBOL(stamp);
    ELLE_DAS_SYMBOL(storing);
  }

  namespace overlay
  {
    namespace kouncil
    {
      using Clock = std::chrono::high_resolution_clock;
      using Time = std::chrono::time_point<Clock>;

      /// BMI helpers
      namespace bmi = boost::multi_index;
      namespace _details
      {
        inline
        model::Address
        peer_id(Overlay::Member const& p)
        {
          return p->id();
        }

        inline
        model::doughnut::Peer*
        peer_ptr(Overlay::Member const& p)
        {
          return p.get();
        }
      }

      /// An overlay that keeps global knowledge locally.
      class Kouncil
        : public Overlay
      {
      /*------.
      | Types |
      `------*/
      public:
        /// Ourselves.
        using Self = Kouncil;
        /// Parent class.
        using Super = Overlay;

        /// Node and blocks address.
        using Address = model::Address;
        /// Address book entry.
        class Entry
        {
        public:
          Entry(Address node, Address block);
          ELLE_ATTRIBUTE_R(Address, node);
          ELLE_ATTRIBUTE_R(Address, block);
        };

        /// Node / owned block addresses mapping.
        using AddressBook = bmi::multi_index_container<
          Entry,
          bmi::indexed_by<
            bmi::hashed_non_unique<
              bmi::const_mem_fun<
                Entry, Address const&, &Entry::node>>,
            bmi::hashed_non_unique<
              bmi::const_mem_fun<
                Entry, Address const&, &Entry::block>>,
            bmi::hashed_unique<
              bmi::identity<Entry>>>>;
        /// Peers by id.
        using Peer = Overlay::Member;
        using Peers =
          bmi::multi_index_container<
          Peer,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::global_fun<
                Peer const&, Address, &_details::peer_id>>>>;

        /// Local node.
        using Local = infinit::model::doughnut::Local;
        /// Remote node.
        using Remote = infinit::model::doughnut::Remote;

        /// A clock.
        using Clock = std::chrono::high_resolution_clock;
        /// A reference date.
        using Time = std::chrono::time_point<Clock>;
        /// The type of our timers.
        using Timer = boost::asio::basic_waitable_timer<Clock>;
        /// Transportable timeout.
        using LamportAge = elle::athena::LamportAge;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        /// Construct a Kouncil with the given backends.
        ///
        /// @arg dht              The owning Doughnut.
        /// @arg local            The local server, null if pure client.
        /// @arg eviction_delay   A time out, in seconds.
        Kouncil(model::doughnut::Doughnut* dht,
                std::shared_ptr<Local> local,
                std::chrono::seconds eviction_delay = std::chrono::seconds{12000});
        /// Destruct a Kouncil.
        ~Kouncil() override;
      protected:
        void
        _cleanup() override;
        ELLE_ATTRIBUTE(bool, cleaning);
      private:
        ///
        void
        _register_local(std::shared_ptr<Local> local);
        /// Check invariants.
        void
        _validate() const;
        ELLE_ATTRIBUTE(std::vector<boost::signals2::scoped_connection>,
                       connections);

      /*-----------.
      | Properties |
      `-----------*/
      protected:
        using Overlay::storing;
        void
        storing(bool v) override;

      /*-------------.
      | Address book |
      `-------------*/
      public:
        /// Global address book.
        ELLE_ATTRIBUTE_R(AddressBook, address_book);
        /// All peers we are currently connected to.
        ELLE_ATTRIBUTE_R(Peers, peers);
        ELLE_ATTRIBUTE(std::default_random_engine, gen, mutable);
      private:
        void
        _broadcast();
        /// Events about blocks: (block, inserted or removed).
        /// If true, inserted, if false removed.
        ELLE_ATTRIBUTE((elle::reactor::Channel<std::pair<Address, bool>>),
                       new_entries);
        ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr, broadcast_thread);

      /*------.
      | Peers |
      `------*/
      public:
        /// A peer we heard about: we are connected to it, or we
        /// expect to be able to connect to it (either because we were
        /// disconnected from it, or because another peer told us
        /// about it).
        struct PeerInfo
          : public elle::Printable::as<PeerInfo>
        {
          PeerInfo(Address id, Endpoints endpoints,
                   int64_t stamp = -1,
                   LamportAge disappearance = {},
                   boost::optional<bool> storing = {});
          PeerInfo(Address id,
                   Endpoints endpoints,
                   Time t,
                   LamportAge disappearance = {},
                   boost::optional<bool> storing = {});
          explicit PeerInfo(NodeLocation const& loc);
          /// Merge peer information in this.
          ///
          /// @param info The endpoints to merge in this, iff they are more
          ///             recent wrt @attribute stamp.
          /// @return Wether the endpoints were replaced.
          bool
          merge(PeerInfo const& from);
          /// Convert to a NodeLocation
          ///
          /// @return A node location with this peer id and endpoints.
          NodeLocation
          location() const;
          void
          print(std::ostream& o) const;

          /// Peer id.
          ELLE_ATTRIBUTE_R(Address, id);
          /// Peer endpoints.
          ELLE_ATTRIBUTE_R(Endpoints, endpoints);
          /// Lamport clock for when the endpoints were established by the peer.
          ELLE_ATTRIBUTE_R(int64_t, stamp);
          /// Time when we lost connection with this peer, or
          /// LamportAge::null() if all is well.
          ELLE_ATTRIBUTE_RWX(LamportAge, disappearance);
          /// Whether that host accepts new blocks
          ELLE_ATTRIBUTE_RW(boost::optional<bool>, storing);
          /// Default model: Serialize non-local information.
          using Model = elle::das::Model<
            PeerInfo,
            decltype(elle::meta::list(symbols::id,
                                      symbols::endpoints,
                                      symbols::stamp,
                                      symbols::disappearance))>;
        };
        /// Peer informations by id.
        using PeerInfos = bmi::multi_index_container<
          PeerInfo,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::const_mem_fun<PeerInfo, Address const&, &PeerInfo::id>>,
            bmi::hashed_non_unique<
              bmi::const_mem_fun<PeerInfo,
                                 boost::optional<bool> const&,
                                 &PeerInfo::storing>>>>;
        /// The peers we heard about.
        ELLE_ATTRIBUTE_R(PeerInfos, infos);

        /// Announcing evicted nodes.
        ELLE_ATTRIBUTE_RX(
          boost::signals2::signal<void (Address id)>, on_eviction);

        /// Nodes with which we lost connection, but keep ready to see
        /// coming back.
        class StaleEndpoint
          : public NodeLocation
          , public elle::Printable::as<StaleEndpoint>
        {
        public:
          StaleEndpoint(NodeLocation const& l);
          /// Reset this and cancel timers.
          void
          clear();
          /// Start reconnection attempts, start the overall eviction timer.
          void
          reconnect(Kouncil& kouncil);
          /// Start one new attempt to reconnect.
          void
          connect(Kouncil& kouncil);
          /// Callback when an attempt timed out.
          void
          failed(Kouncil& kouncil);
          void
          print(std::ostream& stream) const;
          ELLE_ATTRIBUTE(boost::signals2::scoped_connection, slot);
          ELLE_ATTRIBUTE(Timer, retry_timer);
          ELLE_ATTRIBUTE(int, retry_counter);
          ELLE_ATTRIBUTE_X(Timer, evict_timer);
          friend class Kouncil;
        };
        using StaleEndpoints = bmi::multi_index_container<
          StaleEndpoint,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::const_mem_fun<NodeLocation,
                                 Address const&,
                                 &NodeLocation::id>>>>;
        ELLE_ATTRIBUTE_R(StaleEndpoints, stale_endpoints);

      protected:
        void
        _discover(NodeLocations const& peers) override;
        bool
        _discovered(model::Address id) override;
      private:
        /// A new endpoint was discovered.
        void
        _discover(PeerInfo const& pi);
        void
        _discover(PeerInfos const& pis);
        void
        _notify_observers(PeerInfo const& pi);
        void
        _advertise(Remote& r);
        void
        _fetch_entries(Remote& r);
        void
        _perform(std::string const& name, std::function<void()> job);
        /// A peer appears to have disappeared.  We hope to see it again.
        void
        _peer_disconnected(std::shared_ptr<Remote> peer);
        /// Disconnection was too long, forget about this peer.
        void
        _peer_evicted(Address id);
        void
        _peer_connected(std::shared_ptr<Remote> peer);
        StaleEndpoint&
        _remember_stale(NodeLocation const& peer);
        ELLE_ATTRIBUTE(std::vector<elle::reactor::Thread::unique_ptr>, tasks);
        ELLE_ATTRIBUTE_RW(std::chrono::seconds, eviction_delay);

      /*-------.
      | Lookup |
      `-------*/
      protected:
        MemberGenerator
        _allocate(Address address, int n) const override;
        MemberGenerator
        _lookup(Address address, int n, bool fast) const override;
        WeakMember
        _lookup_node(Address address) const override;

      /*-----------.
      | Monitoring |
      `-----------*/
      public:
        std::string
        type_name() const override;
        elle::json::Array
        peer_list() const override;
        elle::json::Object
        stats() const override;

      public:
        elle::json::Json
        query(std::string const& k,
              boost::optional<std::string> const& v) override;
      };
    }
  }
}

ELLE_DAS_SERIALIZE(infinit::overlay::kouncil::Kouncil::PeerInfo);
