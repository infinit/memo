#include <chrono>

#include <elle/log.hh>
#include <elle/make-vector.hh>
#include <elle/range.hh>

#include <reactor/network/exception.hh>

// FIXME: can be avoided with a `Dock` accessor in `Overlay`
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>
#include <infinit/overlay/kouncil/Kouncil.hh>

ELLE_LOG_COMPONENT("infinit.overlay.kouncil.Kouncil")

namespace infinit
{
  namespace overlay
  {
    namespace kouncil
    {
      namespace
      {
        int64_t
        to_milliseconds(Time t)
        {
          return std::chrono::duration_cast<std::chrono::milliseconds>(
                t.time_since_epoch()).count();
        }
      }

      template <typename I>
      struct BoolIterator
        : public I
      {
        BoolIterator(I it, bool v = true)
          : I(it)
          , value(v)
        {}

        operator bool() const
        {
          return this->value;
        }

        bool value;
      };

      template <typename C, typename E>
      auto
      find(C const& c, E const& e)
      {
        auto it = c.find(e);
        return BoolIterator<decltype(it)>(it, it != c.end());
      }

      template <typename C, typename E>
      auto
      find(C& c, E const& e)
      {
        auto it = c.find(e);
        return BoolIterator<decltype(it)>(it, it != c.end());
      }

      /*------.
      | Entry |
      `------*/

      Kouncil::Entry::Entry(Address node, Address block)
        : _node(std::move(node))
        , _block(std::move(block))
      {}

      /*-------------.
      | Construction |
      `-------------*/

      Kouncil::Kouncil(
        model::doughnut::Doughnut* dht,
        std::shared_ptr<Local> local,
        boost::optional<int> eviction_delay)
        : Overlay(dht, local)
        , _cleaning(false)
        , _broadcast_thread(new reactor::Thread(
                              elle::sprintf("%s: broadcast", this),
                              std::bind(&Kouncil::_broadcast, this)))
        , _eviction_delay(eviction_delay.value_or(12000))
      {
        ELLE_TRACE_SCOPE("%s: construct", this);
        if (local)
          _register_local(local);
        // Add client-side Kouncil RPCs.
        this->_connections.emplace_back(
          this->doughnut()->dock().on_connection().connect(
            [this] (model::doughnut::Dock::Connection& r)
            {
              // Notify this node of new peers.
              if (this->doughnut()->version() < elle::Version(0, 8, 0))
                r.rpc_server().add(
                  "kouncil_discover",
                  [this] (NodeLocations const& locs)
                  {
                    PeerInfos infos;
                    for (auto const& l: locs)
                      infos.emplace(l.id(), l.endpoints(), -1);
                    this->_discover(infos);
                  });
              else
                r.rpc_server().add(
                  "kouncil_discover",
                  [this](PeerInfos const& pis)
                  {
                    this->_discover(pis);
                  });
              // Notify this node of new blocks owned by the peer.
              r.rpc_server().add(
                "kouncil_add_entries",
                [this, &r] (std::unordered_set<Address> const& entries)
                {
                  for (auto const& addr: entries)
                    this->_address_book.emplace(r.id(), addr);
                  ELLE_TRACE("%s: added %s entries from %f",
                             this, entries.size(), r.id());
                });
            }));
        // React to peer connection status.
        this->_connections.emplace_back(
          this->doughnut()->dock().on_peer().connect(
            [this] (std::shared_ptr<Remote> peer)
            {
              peer->connected().connect(
                [this, p = std::weak_ptr<Remote>(peer)]
                {
                  this->_peer_connected(ELLE_ENFORCE(p.lock()));
                });
              peer->disconnected().connect(
                [this, p = std::weak_ptr<Remote>(peer)]
                {
                  this->_peer_disconnected(ELLE_ENFORCE(p.lock()));
                });
              this->_peer_connected(peer);
            }));
        this->_validate();
      }

      void
      Kouncil::_register_local(std::shared_ptr<Local> local)
      {
       this->_peers.emplace(local);
       this->_infos.emplace(local->id(), local->server_endpoints(),
                            std::chrono::high_resolution_clock::now());
       for (auto const& key: local->storage()->list())
         this->_address_book.emplace(this->id(), key);
       ELLE_DEBUG("loaded %s entries from storage",
                  this->_address_book.size());
       this->_connections.emplace_back(local->on_store().connect(
         [this] (model::blocks::Block const& b)
         {
           ELLE_DEBUG("%s: register new block %f", this, b.address());
           this->_address_book.emplace(this->id(), b.address());
           this->_new_entries.put(b.address());
         }));
       // Add server-side kouncil RPCs.
       this->_connections.emplace_back(local->on_connect().connect(
         [this] (RPCServer& rpcs)
         {
           // List all blocks owned by this node.
           rpcs.add(
             "kouncil_fetch_entries",
             [this] ()
             {
               auto res = std::unordered_set<Address>{};
               for (auto const& e:
                      elle::as_range(this->_address_book.equal_range(this->id())))
                 res.emplace(e.block());
               return res;
             });
           // Lookup owners of a block on this node.
           rpcs.add(
             "kouncil_lookup",
             [this] (Address const& addr)
             {
               auto res = std::unordered_set<Address>{};
               for (auto const& e:
                      elle::as_range(_address_book.get<1>().equal_range(addr)))
                 res.emplace(e.node());
               return res;
             });
           // Send known peers to this node and retrieve its known peers.
           if (this->doughnut()->version() < elle::Version(0, 8, 0))
             rpcs.add(
               "kouncil_advertise",
               [this, &rpcs](NodeLocations const& peers)
               {
                 ELLE_TRACE_SCOPE(
                   "%s: receive advertisement of %s peers on %s",
                   this, peers.size(), rpcs);
                 this->_discover(peers);
                 auto res = elle::make_vector(
                   this->_infos,
                   [] (PeerInfo const& i) { return i.location(); });
                 ELLE_TRACE_SCOPE("return %s peers", res.size());
                 return res;
               });
             else
               rpcs.add(
                 "kouncil_advertise",
                 [this, &rpcs] (PeerInfos const& infos)
                 {
                   ELLE_TRACE_SCOPE(
                     "%s: receive advertisement of %s peers on %s",
                     this, infos.size(), rpcs);
                   this->_discover(infos);
                   return this->_infos;
                 });
         }));
      }

      Kouncil::~Kouncil()
      {
        ELLE_TRACE_SCOPE("%s: destruct", this);
        // Stop all background operations.
        this->_broadcast_thread->terminate_now();
        {
          // Make sure one of the task does not wake up during the clear() and
          // try to push a new task.
          for (auto& task: this->_tasks)
            task->terminate();
          this->_tasks.clear();
        }
        // Disconnect peers manually. Otherwise, they will be disconnected from
        // their destructor and triggered callbacks (e.g.
        // Kouncil::peer_disconnected) will have dead weak_ptr. Use an
        // intermediate vector because this->_peers is modified while we destroy
        // peers.
        for (auto const& peer: elle::make_vector(this->_peers))
          peer->cleanup();
        // All peers must have been dropped, except local.
        if (this->_peers.size() != (this->doughnut()->local() ? 1u : 0u))
          ELLE_ERR("%s: some peers are still alive after cleanup", this)
          {
            for (auto const& peer: this->_peers)
              ELLE_ERR("%s", peer);
            ELLE_ASSERT_EQ(
              this->_peers.size(), this->doughnut()->local() ? 1u : 0u);
          }
      }

      void
      Kouncil::_cleanup()
      {
        this->_cleaning = true;
        this->_stale_endpoints.clear();
      }

      elle::json::Json
      Kouncil::query(std::string const& k, boost::optional<std::string> const& v)
      {
        auto res = elle::json::Object{};
        if (k == "stats")
        {
          res.emplace("peers", this->peer_list());
          res.emplace("id", elle::sprintf("%s", this->doughnut()->id()));
        }
        return res;
      }

      void
      Kouncil::_validate() const
      {}

      /*-------------.
      | Address book |
      `-------------*/

      void
      Kouncil::_broadcast()
      {
        while (true)
        {
          auto entries = std::unordered_set<model::Address>{};
          auto addr = this->_new_entries.get();
          entries.insert(addr);
          while (!this->_new_entries.empty())
            entries.insert(this->_new_entries.get());
          ELLE_TRACE("%s: broadcast new entry: %f", this, entries);
          this->local()->broadcast<void>(
            "kouncil_add_entries", std::move(entries));
        }
      }

      /*------.
      | Peers |
      `------*/

      void
      Kouncil::_discover(NodeLocations const& peers)
      {
        ELLE_TRACE_SCOPE("%s: discover %s nodes", this, peers.size());
        for (auto const& peer: peers)
        {
          ELLE_DEBUG("endpoints for %f: %s", peer, peer.endpoints());
          if (!find(this->_peers, peer.id()))
            ELLE_TRACE("connect to new %f", peer)
              this->doughnut()->dock().connect(peer, false);
          if (peer.id() != Address::null)
            this->_remember_stale(peer);
        }
      }

      void
      Kouncil::_discover(PeerInfos const& pis)
      {
        for (auto const& pi: pis)
        {
          ELLE_ASSERT_NEQ(pi.id(), Address::null);
          if (pi.id() == this->doughnut()->id())
            // This is us.
            continue;
          if (auto it = find(this->_infos, pi.id()))
          {
            bool change;
            this->_infos.modify(it, [&](PeerInfo& p) { change = p.merge(pi);});
            if (change)
            {
              // New data on a known peer, we need to notify observers
              ELLE_DEBUG("discover new endpoints for %s", pi);
              if (!find(this->_peers, pi.id()))
                this->doughnut()->dock().connect(pi.location());
              this->_remember_stale(pi.location());
              this->_notify_observers(*it);
            }
            else
              ELLE_DEBUG("skip rediscovery of %s", pi);
          }
          else
          {
            ELLE_DEBUG("discover named peer %s", pi);
            this->_infos.insert(pi);
            this->doughnut()->dock().connect(pi.location());
            this->_notify_observers(pi);
          }
        }
      }

      bool
      Kouncil::_discovered(Address id)
      {
        return find(this->_peers, id);
      }

      void
      Kouncil::_peer_connected(std::shared_ptr<Remote> peer)
      {
        ELLE_ASSERT_NEQ(peer->id(), Address::null);
        this->_peers.emplace(peer);
        if (auto it = find(this->_stale_endpoints, peer->id()))
          this->_stale_endpoints.erase(it);
        this->_stale_endpoints.emplace(peer->connection()->location());
        ELLE_TRACE_SCOPE("%s: %f connected", this, peer);
        this->_advertise(*peer);
        this->_fetch_entries(*peer);
        this->on_discover()(peer->connection()->location(), false);
      }

      void
      Kouncil::_peer_disconnected(std::shared_ptr<Remote> peer)
      {
        ELLE_TRACE_SCOPE("%s: %s disconnected", this, peer);
        auto loc = peer->connection()->location();
        auto it = this->_peers.find(peer->id());
        if (it == this->_peers.end() || *it != peer)
        {
          // FIXME: I don't think this should ever happen.
          ELLE_WARN("disconnect on dropped peer %f", peer);
          return;
        }
        if (auto pi = find(this->_infos, peer->id()))
          this->_infos.erase(pi);
        else
          // FIXME: I don't think this should ever happen.
          ELLE_WARN("no info for disconnected peer %f", peer);
        {
          auto its = this->_address_book.equal_range(peer->id());
          this->_address_book.erase(its.first, its.second);
        }
        auto id = peer->id();
        this->_peers.erase(it);
        this->on_disappear()(id, false);
        peer.reset();
        if (!this->_cleaning)
        {
          auto it = ELLE_ENFORCE(find(this->_stale_endpoints, id));
          ELLE_TRACE("retry connection with %s stale endpoints",
                     it->endpoints().size())
          {
            ELLE_DEBUG("endpoints: %s", it->endpoints());
            this->_stale_endpoints.modify(
              it, [this] (StaleEndpoint& e) { e.connect(*this->doughnut()); });
          }
        }
      }

      template<typename E>
      std::vector<int>
      pick_n(E& gen, int size, int count)
      {
        std::vector<int> res;
        while (res.size() < static_cast<unsigned int>(count))
        {
          std::uniform_int_distribution<> random(0, size - 1 - res.size());
          int v = random(gen);
          for (auto r: res)
            if (v >= r)
              ++v;
          res.push_back(v);
          std::sort(res.begin(), res.end());
        }
        return res;
      }

      void
      Kouncil::_remember_stale(NodeLocation const& peer)
      {
        bool changed = false;
        if (auto it = find(this->_stale_endpoints, peer.id()))
        {
          this->_stale_endpoints.modify(
            it,
            [&] (NodeLocation& l)
            {
              changed = l.endpoints().merge(peer.endpoints());
            });
        }
        else
        {
          changed = true;
          this->_stale_endpoints.emplace(peer);
        }
        if (changed)
        {
          ELLE_TRACE("remember new stale endpoints for connected %f", peer)
            ELLE_DEBUG("endpoints: %s", peer.endpoints());
        }
      }

      /*-------.
      | Lookup |
      `-------*/

      reactor::Generator<Overlay::WeakMember>
      Kouncil::_allocate(Address address, int n) const
      {
        return reactor::generator<Overlay::WeakMember>(
          [this, address, n]
          (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
          {
            ELLE_DEBUG("%s: selecting %s nodes from %s peers",
                       this, n, this->_peers.size());
            if (static_cast<unsigned int>(n) >= this->_peers.size())
              for (auto p: this->_peers)
                yield(p);
            else
            {
              std::vector<int> indexes = pick_n(
                this->_gen,
                static_cast<int>(this->_peers.size()),
                n);
              for (auto r: indexes)
                yield(this->peers().get<1>()[r]);
            }
          });
      }

      reactor::Generator<Overlay::WeakMember>
      Kouncil::_lookup(Address address, int n, bool) const
      {
        return reactor::generator<Overlay::WeakMember>(
          [this, address, n]
          (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
          {
            int count = 0;
            for (auto const& entry:
                   elle::as_range(this->_address_book.get<1>().equal_range(address)))
            {
              auto p = this->peers().find(entry.node());
              ELLE_ASSERT(p != this->peers().end());
              yield(*p);
              if (++count >= n)
                break;
            }
            if (count == 0)
            {
              ELLE_TRACE_SCOPE("%s: block %f not found, checking all %s peers",
                               this, address, this->peers().size());
              for (auto peer: this->peers())
              {
                // FIXME: handle local !
                if (auto r = std::dynamic_pointer_cast<
                    Remote>(peer))
                {
                  auto lookup =
                    r->make_rpc<std::unordered_set<Address> (Address)>(
                      "kouncil_lookup");
                  try
                  {
                    for (auto node: lookup(address))
                    {
                      try
                      {
                        ELLE_DEBUG("peer %f says node %f holds block %f",
                                   r->id(), node, address);
                        yield(this->lookup_node(node));
                        if (++count >= n)
                          break;
                      }
                      catch (NodeNotFound const&)
                      {
                        ELLE_WARN("node %f is said to hold block %f "
                                  "but is unknown to us", node, address);
                      }
                    }
                  }
                  catch (reactor::network::Exception const& e)
                  {
                    ELLE_DEBUG("skipping peer with network issue: %s (%s)",
                               peer, e);
                    continue;
                  }
                  if (count > 0)
                    return;
                }
              }
            }
          });
      }

      Overlay::WeakMember
      Kouncil::_lookup_node(Address id) const
      {
        if (auto it = find(this->_peers, id))
          return *it;
        else
          return Overlay::WeakMember();
      }

      void
      Kouncil::_perform(std::string const& name, std::function<void()> job)
      {
        this->_tasks.emplace_back(new reactor::Thread(name, job));
        for (unsigned i=0; i<this->_tasks.size(); ++i)
          if (!this->_tasks[i] || this->_tasks[i]->done())
          {
            std::swap(this->_tasks[i], this->_tasks[this->_tasks.size()-1]);
            this->_tasks.pop_back();
            --i;
          }
      }

      /*-----------.
      | Monitoring |
      `-----------*/

      std::string
      Kouncil::type_name()
      {
        return "kouncil";
      }

      elle::json::Array
      Kouncil::peer_list()
      {
        auto res = elle::json::Array{};
        for (auto const& p: this->peers())
          if (auto r = dynamic_cast<Remote const*>(p.get()))
          {
            auto endpoints = elle::json::Array{};
            for (auto const& e: r->endpoints())
              endpoints.push_back(elle::sprintf("%s", e));
            res.push_back(elle::json::Object{
              { "id", elle::sprintf("%x", r->id()) },
              { "endpoints",  endpoints },
              { "connected",  true},
            });
          }
        return res;
      }

      elle::json::Object
      Kouncil::stats()
      {
        elle::json::Object res;
        res["type"] = this->type_name();
        res["id"] = elle::sprintf("%s", this->doughnut()->id());
        return res;
      }

      void
      Kouncil::_notify_observers(PeerInfos::value_type const& pi)
      {
        if (!this->local())
          return;
        // FIXME: One Thread and one RPC to notify them all by batch, not one by
        // one.
        this->_perform("notify observers",
          [this, pi=pi]
          {
            try
            {
              ELLE_DEBUG("%s: notify observers of %s", this, pi);
              if (this->doughnut()->version() < elle::Version(0, 8, 0))
              {
                NodeLocations locs;
                locs.emplace_back(pi.location());
                this->local()->broadcast<void>("kouncil_discover", locs);
              }
              else
              {
                PeerInfos pis;
                pis.insert(pi);
                this->local()->broadcast<void>("kouncil_discover", pis);
              }
            }
            catch (elle::Error const& e)
            {
              ELLE_WARN("%s: unable to notify observer: %s", this, e);
            }
          });
      }

      void
      Kouncil::_advertise(Remote& r)
      {
        auto send_peers = bool(this->doughnut()->local());
        ELLE_TRACE_SCOPE("send %s peers and fetch known peers of %s",
                         send_peers ? this->_infos.size() : 0, r);
        if (send_peers)
          ELLE_DUMP("peers: %s", this->_infos);
        try
        {
          if (this->doughnut()->version() < elle::Version(0, 8, 0))
          {
            auto advertise = r.make_rpc<NodeLocations (NodeLocations const&)>(
              "kouncil_advertise");
            auto locations = send_peers ?
              elle::make_vector(
                this->_infos,
                [] (PeerInfo const& i) { return i.location(); }) :
              NodeLocations();
            auto peers = advertise(locations);
            ELLE_TRACE("fetched %s peers", peers.size());
            ELLE_DEBUG("peers: %f", peers);
            PeerInfos infos;
            for (auto const& l: peers)
              infos.emplace(l.id(), l.endpoints(), -1);
            this->_discover(infos);
          }
          else
          {
            auto advertise =
              r.make_rpc<PeerInfos(PeerInfos const&)>("kouncil_advertise");
            auto npi = advertise(send_peers ? this->_infos : PeerInfos());
            ELLE_TRACE("fetched %s peers", npi.size());
            ELLE_DUMP("peers: %s", npi);
            this->_discover(npi);
          }
        }
        catch (reactor::network::Exception const& e)
        {
          ELLE_TRACE("%s: network exception advertising %s: %s", this, r, e);
          // nothing to do, disconnected() will be emited and handled
        }
      }

      void
      Kouncil::_fetch_entries(Remote& r)
      {
        auto fetch = r.make_rpc<std::unordered_set<Address> ()>(
          "kouncil_fetch_entries");
        auto entries = fetch();
        ELLE_ASSERT_NEQ(r.id(), Address::null);
        for (auto const& b: entries)
          this->_address_book.emplace(r.id(), b);
        ELLE_DEBUG("added %s entries from %f", entries.size(), r);
      }

      boost::optional<Endpoints>
      Kouncil::_endpoints_refetch(Address id)
      {
        if (auto it = find(this->_infos, id))
        {
          ELLE_DEBUG("updating endpoints for %s with %s entries",
                     id, it->endpoints().size());
          return it->endpoints();
        }
        return boost::none;
      }

      /*---------.
      | PeerInfo |
      `---------*/

      Kouncil::PeerInfo::PeerInfo(Address id,
                                  Endpoints endpoints,
                                  int64_t stamp)
        : _id(id)
        , _endpoints(std::move(endpoints))
        , _stamp(stamp)
        , _last_seen(std::chrono::high_resolution_clock::now())
        , _last_contact_attempt(std::chrono::high_resolution_clock::now())
      {}

      Kouncil::PeerInfo::PeerInfo(Address id,
                                  Endpoints endpoints,
                                  Time t)
        : PeerInfo(id, endpoints, to_milliseconds(t))
      {}

      bool
      Kouncil::PeerInfo::merge(Kouncil::PeerInfo const& from)
      {
        if ((this->_stamp < from._stamp || from._stamp == -1) &&
            this->_endpoints != from._endpoints)
        {
          this->_endpoints = from._endpoints;
          this->_stamp = from._stamp;
          return true;
        }
        else
          return false;
      }

      NodeLocation
      Kouncil::PeerInfo::location() const
      {
        return NodeLocation(this->_id, this->_endpoints);
      }

      void
      Kouncil::PeerInfo::print(std::ostream& o) const
      {
        elle::fprintf(o, "%s(%f)", elle::type_info(*this), this->id());
      }

      static const elle::TypeInfo::RegisterAbbrevation
      _kouncil_abbr("kouncil::Kouncil", "Kouncil");

      /*--------------.
      | StaleEndpoint |
      `--------------*/

      Kouncil::StaleEndpoint::StaleEndpoint(NodeLocation const& l)
        : NodeLocation(l)
        , _retry_timer(reactor::scheduler().io_service())
        , _retry_counter(0)
      {}

      void
      Kouncil::StaleEndpoint::connect(model::doughnut::Doughnut& dht)
      {
        auto c = dht.dock().connect(*this);
        this->_slot = c->on_disconnection().connect(
          std::bind(&StaleEndpoint::failed, this, boost::ref(dht)));
      }

      void
      Kouncil::StaleEndpoint::failed(model::doughnut::Doughnut& dht)
      {
        // FIXME: make that configurable
        if (++this->_retry_counter > 10)
          return;
        this->_retry_timer.expires_from_now(
          boost::posix_time::seconds(1) * std::pow(2, this->_retry_counter));
        this->_retry_timer.async_wait(
          [this, &dht] (boost::system::error_code const& e)
          {
            if (!e)
              this->connect(dht);
          });
      }
    }
  }
}
