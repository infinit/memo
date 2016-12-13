#ifndef INFINIT_OVERLAY_KOORDINATE_HH
# define INFINIT_OVERLAY_KOORDINATE_HH

# include <infinit/overlay/Overlay.hh>

namespace infinit
{
  namespace overlay
  {
    namespace koordinate
    {
      /** An overlay that aggregates several underlying overlays.
       *
       *  Koordinates lets a node serve several overlays for other to query,
       *  and forwards local requets to the first one. Future evolution may
       *  enable to leverage several backend overlays depending on policies.
       */
      class Koordinate
        : public Overlay
      {
      /*------.
      | Types |
      `------*/
      public:
        /// Ourselves.
        typedef Koordinate Self;
        /// Parent class.
        typedef Overlay Super;
        /// Underlying overlay to run and forward requests to.
        typedef std::unique_ptr<Overlay> Backend;
        /// Set of underlying overlays.
        typedef std::vector<Backend> Backends;

      /*-------------.
      | Construction |
      `-------------*/
      public:
        /** Construct a Koordinate with the given backends.
         *
         *  @arg   dht      The owning Doughnut.
         *  @arg   local    The local server, null if pure client.
         *  @arg   backends The underlying overlays.
         *  @throw elle::Error if \a backends is empty.
         */
        Koordinate(model::doughnut::Doughnut* dht,
                   std::shared_ptr<infinit::model::doughnut::Local> local,
                   Backends backends);
        /// Destruct a Koordinate.
        virtual
        ~Koordinate();
        /// The underlying overlays.
        ELLE_ATTRIBUTE(Backends, backends);
      private:
        /// Check invariants.
        void
        _validate() const;

      /*------.
      | Peers |
      `------*/
      protected:
        virtual
        void
        _discover(NodeLocations const& peers) override;

      /*-------.
      | Lookup |
      `-------*/
      protected:
        reactor::Generator<WeakMember>
        _allocate(model::Address address, int n) const override;
        virtual
        reactor::Generator<std::pair<model::Address, WeakMember>>
        _lookup(std::vector<model::Address> const& addrs, int n) const override;
        virtual
        reactor::Generator<WeakMember>
        _lookup(model::Address address, int n, bool fast) const override;
        virtual
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
      };
    }
  }
}

#endif
