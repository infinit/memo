#pragma once

#include <random>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <elle/unordered_map.hh>

#include <infinit/model/doughnut/Peer.hh>
#include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace symbols
  {
    DAS_SYMBOL(endpoints);
    DAS_SYMBOL(stamp);
  }
}
namespace infinit
{
  namespace overlay
  {
    namespace kouncil
    {
      using Time = std::chrono::time_point<std::chrono::high_resolution_clock>;
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

      /** An overlay that keeps global knowledge locally.
       */
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
            bmi::sequenced<>
          >>;
        /// Peers by id.
        using Peer = Overlay::Member;
        using Peers =
          bmi::multi_index_container<
          Peer,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::global_fun<
                Peer const&, Address, &_details::peer_id>>,
            bmi::random_access<>>>;

        using Local = infinit::model::doughnut::Local;
        using Remote = infinit::model::doughnut::Remote;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        /** Construct a Kouncil with the given backends.
         *
         *  @arg   dht      The owning Doughnut.
         *  @arg   local    The local server, null if pure client.
         */
        Kouncil(model::doughnut::Doughnut* dht,
                std::shared_ptr<Local> local,
                boost::optional<int> eviction_delay = boost::none);
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

      /*-------------.
      | Address book |
      `-------------*/
      public:
        /// Global address book.
        ELLE_ATTRIBUTE_R(AddressBook, address_book);
        /// All known peers.
        ELLE_ATTRIBUTE_R(Peers, peers);
        ELLE_ATTRIBUTE(std::default_random_engine, gen, mutable);
      private:
        void
        _broadcast();
        ELLE_ATTRIBUTE(reactor::Channel<Address>, new_entries);
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, broadcast_thread);

      /*------.
      | Peers |
      `------*/
      public:
        struct PeerInfo
          : public elle::Printable::as<PeerInfo>
        {
          PeerInfo(Address id, Endpoints endpoints, int64_t stamp);
          PeerInfo(Address id, Endpoints endpoints, Time t);
          /** Merge peer informations in this.
           *
           *  @param info The endpoints to merge in this, iff they are more
           *              recent wrt @attribute stamp.
           *  @return Wether the endpoints were replaced.
           */
          bool
          merge(PeerInfo const& from);
          /** Convert to a NodeLocation
           *
           *  @return A node location with this peer id and endpoints.
           */
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
          /// Last time we saw the peer online.
          ELLE_ATTRIBUTE_RW(Time, last_seen);
          /// Last time we tried to contact the peer, if offline.
          // FIXME: drop
          ELLE_ATTRIBUTE_RW(Time, last_contact_attempt);
          /// Default model: Serialize non-local information.
          using Model = das::Model<
            PeerInfo,
            decltype(elle::meta::list(symbols::id,
                                      symbols::endpoints,
                                      symbols::stamp))>;
        };
        /// Peer informations by id.
        using PeerInfos = bmi::multi_index_container<
          PeerInfo,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::const_mem_fun<PeerInfo, Address const&, &PeerInfo::id>>>>;
        ELLE_ATTRIBUTE_R(PeerInfos, infos);
        class StaleEndpoint
          : public NodeLocation
        {
        public:
          StaleEndpoint(NodeLocation const& l);
          void
          connect(model::doughnut::Doughnut& dht);
          void
          failed(model::doughnut::Doughnut& dht);
          ELLE_ATTRIBUTE(boost::signals2::scoped_connection, slot);
          ELLE_ATTRIBUTE(boost::asio::deadline_timer, retry_timer);
          ELLE_ATTRIBUTE(int, retry_counter);
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
        void
        _discover(PeerInfos const& pis);
        void
        _notify_observers(PeerInfos::value_type const& pi);
        void
        _advertise(Remote& r);
        void
        _fetch_entries(Remote& r);
        boost::optional<Endpoints>
        _endpoints_refetch(Address id);
        void
        _perform(std::string const& name, std::function<void()> job);
        void
        _peer_disconnected(std::shared_ptr<Remote> peer);
        void
        _peer_connected(std::shared_ptr<Remote> peer);
        void
        _remember_stale(NodeLocation const& peer);
        ELLE_ATTRIBUTE(std::vector<reactor::Thread::unique_ptr>, tasks);
        ELLE_ATTRIBUTE(std::chrono::seconds, eviction_delay);

      /*-------.
      | Lookup |
      `-------*/
      protected:
        reactor::Generator<WeakMember>
        _allocate(Address address, int n) const override;
        reactor::Generator<WeakMember>
        _lookup(Address address, int n, bool fast) const override;
        WeakMember
        _lookup_node(Address address) const override;

      /*-----------.
      | Monitoring |
      `-----------*/
      public:
        std::string
        type_name() override;
        elle::json::Array
        peer_list() override;
        elle::json::Object
        stats() override;

      public:
        elle::json::Json
        query(std::string const& k,
              boost::optional<std::string> const& v) override;
      };
    }
  }
}

DAS_SERIALIZE(infinit::overlay::kouncil::Kouncil::PeerInfo);
