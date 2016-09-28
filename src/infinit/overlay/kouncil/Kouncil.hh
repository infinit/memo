#ifndef INFINIT_OVERLAY_KOUNCIL_HH
# define INFINIT_OVERLAY_KOUNCIL_HH

# include <boost/multi_index/hashed_index.hpp>
# include <boost/multi_index/mem_fun.hpp>
# include <boost/multi_index/sequenced_index.hpp>
# include <boost/multi_index_container.hpp>

# include <elle/unordered_map.hh>

# include <infinit/overlay/Overlay.hh>
# include <infinit/overlay/Overlay.hh>


namespace infinit
{
  namespace overlay
  {
    namespace kouncil
    {
      /** An overlay that aggregates several underlying overlays.
       *
       *  Kouncils lets a node serve several overlays for other to query,
       *  and forwards local requets to the first one. Future evolution may
       *  enable to leverage several backend overlays depending on policies.
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
        using Peers = elle::unordered_map<model::Address, Overlay::Member>;

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
      private:
        void
        _broadcast();
        ELLE_ATTRIBUTE(reactor::Channel<model::Address>, new_entries);
        ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, broadcast_thread);

      /*------.
      | Peers |
      `------*/
      protected:
        virtual
        void
        _discover(NodeLocations const& peers) override;
        void
        _discover(Overlay::Member peer);

      /*-------.
      | Lookup |
      `-------*/
      protected:
        virtual
        reactor::Generator<WeakMember>
        _lookup(model::Address address, int n, Operation op) const override;
        virtual
        WeakMember
        _lookup_node(model::Address address) override;
      };
    }
  }
}

#endif
