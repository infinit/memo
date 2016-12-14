#ifndef INFINIT_OVERLAY_KOUNCIL_HH
# define INFINIT_OVERLAY_KOUNCIL_HH

# include <random>

# include <boost/multi_index/global_fun.hpp>
# include <boost/multi_index/hashed_index.hpp>
# include <boost/multi_index/mem_fun.hpp>
# include <boost/multi_index/random_access_index.hpp>
# include <boost/multi_index/sequenced_index.hpp>
# include <boost/multi_index_container.hpp>

# include <elle/unordered_map.hh>

# include <infinit/model/doughnut/Peer.hh>
# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace symbols
  {
    DAS_SYMBOL(endpoints_stamped);
    DAS_SYMBOL(endpoints_unstamped);
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
        typedef Kouncil Self;
        /// Parent class.
        typedef Overlay Super;
        /// Address book entry
        class Entry
        {
        public:
          Entry(model::Address node, model::Address block);
          ELLE_ATTRIBUTE_R(model::Address, node);
          ELLE_ATTRIBUTE_R(model::Address, block);
        };
        /// Node / owned block addresses mapping.
        using AddressBook = boost::multi_index::multi_index_container<
          Entry,
          boost::multi_index::indexed_by<
            boost::multi_index::hashed_non_unique<
              boost::multi_index::const_mem_fun<
                Entry, model::Address const&, &Entry::node>>,
            boost::multi_index::hashed_non_unique<
              boost::multi_index::const_mem_fun<
                Entry, model::Address const&, &Entry::block>>,
            boost::multi_index::sequenced<>
          >>;
        /// Peers by id
        using Peer = Overlay::Member;
        using Peers =
          bmi::multi_index_container<
          Peer,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::global_fun<
                Peer const&, model::Address, &_details::peer_id> >,
            bmi::random_access<> > >;

        using DisconectedPeers =
          bmi::multi_index_container<
          Peer,
          bmi::indexed_by<
            bmi::hashed_unique<
              bmi::global_fun<
                Peer const&, model::Address, &_details::peer_id> >,
            bmi::hashed_unique<
              bmi::global_fun<
                Peer const&, model::doughnut::Peer*, &_details::peer_ptr> >>>;
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
                std::shared_ptr<infinit::model::doughnut::Local> local);
        /// Destruct a Kouncil.
        virtual
        ~Kouncil();
      private:
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
        ELLE_ATTRIBUTE(DisconectedPeers, disconnected_peers);
        ELLE_ATTRIBUTE(std::default_random_engine, gen, mutable);
      private:
        void
        _broadcast();
        ELLE_ATTRIBUTE(reactor::Channel<model::Address>, new_entries);
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, broadcast_thread);

      /*------.
      | Peers |
      `------*/
      public:
        struct PeerInfo
        {
          Endpoints endpoints_stamped;
          Endpoints endpoints_unstamped;
          int64_t stamp;
          bool merge(PeerInfo const& from);

          // local info
          Time last_seen;
          Time last_contact_attempt;
          using Model = das::Model<
            PeerInfo,
            elle::meta::List<symbols::Symbol_endpoints_stamped,
                             symbols::Symbol_endpoints_unstamped,
                             symbols::Symbol_stamp>>;
        };
        using PeerInfos = std::unordered_map<model::Address, PeerInfo>;
        NodeLocations
        peers_locations() const;
        ELLE_ATTRIBUTE_R(PeerInfos, infos);
      protected:
        virtual
        void
        _discover(NodeLocations const& peers) override;
      private:
        void
        _discover(Overlay::Member peer);
        void
        _discover(PeerInfos::value_type const& peer);
        void
        _discover(PeerInfos const& pis);
        void
        _notify_observers(PeerInfos::value_type const& pi);
        boost::optional<Endpoints>
        _endpoints_refetch(model::Address id);
        void
        _perform(std::string const& name, std::function<void()> job);
        void
        _peer_disconnected(model::doughnut::Peer* peer);
        void
        _watcher();
        reactor::Thread::unique_ptr _watcher_thread;
        ELLE_ATTRIBUTE(std::vector<reactor::Thread::unique_ptr>, tasks);

      /*-------.
      | Lookup |
      `-------*/
      protected:
        reactor::Generator<WeakMember>
        _allocate(model::Address address, int n) const override;
        reactor::Generator<WeakMember>
        _lookup(model::Address address, int n, bool fast) const override;
        WeakMember
        _lookup_node(model::Address address) const override;

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

#endif
