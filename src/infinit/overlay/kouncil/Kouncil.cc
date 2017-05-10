#include <chrono>

#include <boost/range/algorithm/sort.hpp>
#include <boost/range/algorithm_ext/iota.hpp>

#include <elle/find.hh>
#include <elle/log.hh>
#include <elle/make-vector.hh>
#include <elle/random.hh>
#include <elle/range.hh>

#include <elle/das/tuple.hh>

#include <elle/reactor/network/Error.hh>

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
      using Address = model::Address;
      /// A set of Address, used in RPCs.
      using AddressSet = std::unordered_set<Address>;
      using EntryChangeSet = std::unordered_map<Address, bool>;
      /// Peer configuration
      using Configuration = elle::das::tuple<
        decltype(symbols::storing)::Formal<bool>>;

      namespace
      {
        int64_t
        to_milliseconds(Time t)
        {
          return std::chrono::duration_cast<std::chrono::milliseconds>(
                t.time_since_epoch()).count();
        }
      }

      /*------.
      | Entry |
      `------*/

      Kouncil::Entry::Entry(Address node, Address block)
        : _node(std::move(node))
        , _block(std::move(block))
      {}

      static
      std::size_t
      hash_value(Kouncil::Entry const& entry)
      {
        std::size_t seed = 0;
        boost::hash_combine(seed, entry.node());
        boost::hash_combine(seed, entry.block());
        return seed;
      }

      static
      bool
      operator ==(Kouncil::Entry const& lhs, Kouncil::Entry const& rhs)
      {
        return lhs.node() == rhs.node() && lhs.block() == rhs.block();
      }

      static
      std::ostream&
      operator <<(std::ostream& os, Kouncil::Entry const& e)
      {
        return elle::fprintf(os, is_fixed(os) ? "%f @ %f" : "%s @ %s",
                             e.block(), e.node());
      }

      /*-------------.
      | Construction |
      `-------------*/

      Kouncil::Kouncil(model::doughnut::Doughnut* dht,
                       std::shared_ptr<Local> local,
                       boost::optional<std::chrono::seconds> eviction_delay)
        : Overlay(dht, local)
        , _cleaning(false)
        , _broadcast_thread(new elle::reactor::Thread(
                              elle::sprintf("%s: broadcast", this),
                              [this] { this->_broadcast(); }))
        , _eviction_delay(
          eviction_delay ? *eviction_delay : std::chrono::seconds{200 * 60})
      {
        ELLE_TRACE_SCOPE("%s: construct", this);
        ELLE_DEBUG("Eviction delay: %s", _eviction_delay);
        if (_eviction_delay < std::chrono::seconds(3))
          ELLE_WARN("Eviction is dangerously low, peers might be evicted"
                    " before any reconnection attempt");
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
                    this->_discover(PeerInfos{locs.begin(), locs.end()});
                  });
              else
                r.rpc_server().add(
                  "kouncil_discover",
                  [this](PeerInfos const& pis)
                  {
                    this->_discover(pis);
                  });
              // Notify this node of new blocks owned by the peer.
              if (this->doughnut()->version() >= elle::Version(0, 8, 0))
                r.rpc_server().add(
                  "kouncil_change_entries",
                  [this, &r] (EntryChangeSet const& entries)
                  {
                    for (auto const& entry: entries)
                      if (entry.second)
                        this->_address_book.emplace(r.id(), entry.first);
                      else
                      {
                        Entry e(r.id(), entry.first);
                        this->_address_book.get<2>().erase(e);
                      }
                    ELLE_TRACE("%s: added/removed %s entries from %f",
                               this, entries.size(), r.id());
                  });
              else
                r.rpc_server().add(
                  "kouncil_add_entries",
                  [this, &r] (AddressSet const& entries)
                  {
                    for (auto const& addr: entries)
                      this->_address_book.emplace(r.id(), addr);
                    ELLE_TRACE("%s: added %s entries from %f",
                               this, entries.size(), r.id());
                  });
              if (this->doughnut()->version() >= elle::Version(0, 8, 0))
              {
                r.rpc_server().add(
                  "kouncil_configure",
                  [this, &r] (Configuration const& conf)
                  {
                    ELLE_TRACE_SCOPE(
                      "%s: peer %f storing: %s", this, r.id(), conf.storing);
                    auto info = ELLE_ENFORCE(elle::find(this->_infos, r.id()));
                    this->_infos.modify(
                      info, [&] (auto& p) { p.storing(conf.storing); });
                  });
              }
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
       auto local_endpoints = local->server_endpoints();
       ELLE_DEBUG("local endpoints: %s", local_endpoints);
       this->_infos.emplace(local->id(), local_endpoints, Clock::now(),
                            LamportAge(), this->storing());
       for (auto const& key: local->storage()->list())
         this->_address_book.emplace(this->id(), key);
       ELLE_DEBUG("loaded %s entries from storage",
                  this->_address_book.size());
       this->_connections.emplace_back(local->on_store().connect(
         [this] (model::blocks::Block const& b)
         {
           ELLE_DEBUG("%s: register new block %f", this, b.address());
           this->_address_book.emplace(this->id(), b.address());
           this->_new_entries.emplace(b.address(), true);
         }));
       this->_connections.emplace_back(local->on_remove().connect(
         [this] (model::blocks::Block const& b)
         {
           ELLE_DEBUG("%s: unregister block %f", this, b.address());
           auto entry = Entry(this->id(), b.address());
           ELLE_ENFORCE_EQ(this->_address_book.get<2>().erase(entry), 1u);
           this->_new_entries.emplace(b.address(), false);
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
               auto res = AddressSet{};
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
               auto res = AddressSet{};
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
                 ELLE_DUMP("peers: %s", peers);
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
                   ELLE_DUMP("peers: %s", infos);
                   this->_discover(infos);
                   return std::make_pair(
                     this->_infos, Configuration(this->storing()));
                 });
         }));
      }

      Kouncil::~Kouncil()
      {
        ELLE_TRACE_SCOPE("%s: destruct", this);
        // Stop all background operations.
        this->_broadcast_thread->terminate_now();
        {
          // Make sure none of the tasks will wake up during the
          // clear() and try to push a new task.
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
        // All peers must have been dropped, possibly except local.
        auto expected_size = this->doughnut()->local() ? 1u : 0u;
        if (this->_peers.size() != expected_size)
          ELLE_ERR("%s: some peers are still alive after cleanup", this)
          {
            for (auto const& peer: this->_peers)
              ELLE_ERR("%s", peer);
            ELLE_ASSERT_EQ(this->_peers.size(), expected_size);
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
        if (k == "stats")
          return elle::json::Object
            {
              {"peers", this->peer_list()},
              {"id", elle::sprintf("%s", this->doughnut()->id())},
              {"infos", elle::json::make_array(_infos,
                       [](auto& pi) -> elle::json::Object
                       {
                         return
                           {
                             {"id", elle::sprintf("%s", pi.id())},
                             {"endpoints", elle::sprintf("%s", pi.endpoints())},
                             {"stamp", elle::sprintf("%s", pi.stamp())},
                             {"disappearance", elle::sprintf("%s", pi.disappearance())},
                           };
                       })
                },
            };
        else
          return {};
      }

      void
      Kouncil::_validate() const
      {}

      /*-----------.
      | Properties |
      `-----------*/

      void
      Kouncil::storing(bool storing)
      {
        if (storing != this->storing())
        {
          this->Overlay::storing(storing);
          // FIXME: don't block if some node fail to respond
          this->local()->broadcast<void>(
            "kouncil_configure", Configuration(storing));
        }
      }

      /*-------------.
      | Address book |
      `-------------*/

      void
      Kouncil::_broadcast()
      {
        while (true)
        {
          // Get all the available new entries, waiting for at least one.
          if (this->doughnut()->version() >= elle::Version(0, 8, 0))
          {
            auto entries = [&]
              {
                auto res = EntryChangeSet{};
                do
                  res.emplace(this->_new_entries.get());
                while (!this->_new_entries.empty());
                return res;
              }();
            ELLE_TRACE("%s: broadcast entry changes: %f", this, entries);
            this->local()->broadcast<void>(
              "kouncil_change_entries", std::move(entries));
          }
          else
          {
            auto entries = [&]
              {
                auto res = AddressSet{};
                do
                {
                  auto e = this->_new_entries.get();
                  if (e.second)
                    res.emplace(e.first);
                }
                while (!this->_new_entries.empty());
                return res;
              }();
            ELLE_TRACE("%s: broadcast new entry: %f", this, entries);
            this->local()->broadcast<void>(
              "kouncil_add_entries", std::move(entries));
          }
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
          if (!elle::find(this->_peers, peer.id()))
            ELLE_TRACE("connect to new %f", peer)
              this->doughnut()->dock().connect(peer);
          if (peer.id() != Address::null)
            this->_remember_stale(peer);
        }
      }

      void
      Kouncil::_discover(PeerInfo const& pi)
      {
        ELLE_ASSERT_NEQ(pi.id(), Address::null);
        auto changed = true;
        if (auto it = elle::find(this->_infos, pi.id()))
          this->_infos.modify(it, [&](auto& p) { changed = p.merge(pi); });
        else
          this->_infos.insert(pi);
        if (changed)
        {
          ELLE_DEBUG("discover new endpoints for %s", pi);
          auto& stale = this->_remember_stale(pi.location());
          if (!elle::find(this->_peers, pi.id()))
          {
            auto c = this->doughnut()->dock().connect(pi.location());
            stale._slot = c->on_disconnection().connect(
              [&stale, this]
              {
                stale.failed(*this);
              });
          }
          this->_notify_observers(pi);
        }
        else
          ELLE_DEBUG("skip rediscovery of %s", pi);
      }

      void
      Kouncil::_discover(PeerInfos const& pis)
      {
        for (auto const& pi: pis)
          if (pi.id() != this->doughnut()->id())
            _discover(pi);
      }

      bool
      Kouncil::_discovered(Address id)
      {
        return elle::find(this->_peers, id);
      }

      void
      Kouncil::_peer_connected(std::shared_ptr<Remote> peer)
      {
        ELLE_TRACE_SCOPE("%s: %f connected", this, peer);
        ELLE_ASSERT_NEQ(peer->id(), Address::null);
        this->_peers.emplace(peer);
        if (auto it = elle::find(this->_stale_endpoints, peer->id()))
          // Stop reconnection/eviction timers.
          this->_stale_endpoints.modify(
            it, [] (StaleEndpoint& e) { e.clear(); });
        else if (not this->_cleaning)
          this->_stale_endpoints.emplace(peer->connection()->location());
        // The peer can be missing from `_infos` for external discoveries.
        if (auto info = elle::find(this->_infos, peer->id()))
          this->_infos.modify(
            info,
            [this] (PeerInfo& pi)
            {
              pi.storing(true);
            });
        this->_advertise(*peer);
        this->_fetch_entries(*peer);
        ELLE_DUMP("%f: signaling connection to %f",
                  this, peer->connection()->location());
        this->on_discovery()(peer->connection()->location(), false);
      }

      void
      Kouncil::_peer_disconnected(std::shared_ptr<Remote> peer)
      {
        ELLE_TRACE_SCOPE("%s: %s disconnected", this, peer);
        auto const id = peer->id();
        // The peer can be missing from `_infos` if the error occurs during the
        // first advertise.
        if (auto info = elle::find(this->_infos, id))
          this->_infos.modify(
            info,
            [this] (PeerInfo& pi)
            {
              pi.disappearance().start();
              pi.storing(boost::none);
            });
        this->_peers.erase(id);
        this->_address_book.erase(id);
        peer.reset();
        if (!this->_cleaning)
        {
          auto it = ELLE_ENFORCE(elle::find(this->_stale_endpoints, id));
          ELLE_TRACE("retry connection with %s stale endpoints",
                     it->endpoints().size())
          {
            ELLE_DEBUG("endpoints: %s", it->endpoints());
            this->_stale_endpoints.modify(
              it, [this] (StaleEndpoint& e) { e.reconnect(*this); });
          }
        }
        this->on_disappearance()(id, false);
      }

      void
      Kouncil::_peer_evicted(Address const id)
      {
        ELLE_TRACE_SCOPE("%s: %f evicted", this, id);
        assert(!this->_discovered(id));
        this->_infos.erase(id);
        this->_address_book.erase(id);
        this->_stale_endpoints.erase(id);
        this->on_eviction()(id);
      }

      Kouncil::StaleEndpoint&
      Kouncil::_remember_stale(NodeLocation const& peer)
      {
        bool changed = false;
        StaleEndpoint* res = nullptr;
        if (auto it = elle::find(this->_stale_endpoints, peer.id()))
        {
          this->_stale_endpoints.modify(
            it,
            [&] (StaleEndpoint& l)
            {
              res = &l;
              changed = l.endpoints().merge(peer.endpoints());
            });
        }
        else
        {
          changed = true;
          auto emplaced = this->_stale_endpoints.emplace(peer);
          ELLE_ASSERT(emplaced.second);
          res = elle::unconst(&*emplaced.first);
        }
        if (changed)
        {
          ELLE_TRACE("remember new stale endpoints for connected %f", peer)
            ELLE_DEBUG("endpoints: %s", peer.endpoints());
        }
        ELLE_ASSERT(res);
        return *res;
      }

      /*-------.
      | Lookup |
      `-------*/

      auto
      Kouncil::_allocate(Address address, int n) const
        -> MemberGenerator
      {
        return [this, address, n](MemberGenerator::yielder const& yield)
          {
            if (this->doughnut()->version() < elle::Version(0, 8, 0))
            {
              auto const size = static_cast<int>(this->_peers.size());
              ELLE_DEBUG("%s: selecting %s nodes from %s peers",
                         this, n, size);
              auto const indexes = elle::pick_n(
                std::min(n, size), size, this->_gen);
              for (auto r: indexes)
                yield(this->peers().get<1>()[r]);
            }
            else
            {
              // Select only nodes that are ready to store.
              auto const range =
                elle::as_range(this->_infos.get<1>().equal_range(
                                 boost::optional<bool>(true)));
              auto const size = int(boost::size(range));
              ELLE_DEBUG_SCOPE("selecting %s nodes from %s peers", n, size);
              auto const indexes = elle::pick_n(std::min(n, size), size, this->_gen);
              auto it = range.begin();
              auto prev = 0;
              for (auto r: indexes)
              {
                std::advance(it, r - prev);
                prev = r;
                yield(*ELLE_ENFORCE(elle::find(this->peers(), it->id())));
              }
            }
          };
      }

      auto
      Kouncil::_lookup(Address address, int n, bool) const
        -> MemberGenerator
      {
        return [this, address, n](MemberGenerator::yielder const& yield)
          {
            int count = 0;
            for (auto const& entry:
                   elle::as_range(this->_address_book.get<1>().equal_range(address)))
              if (auto p = elle::find(this->peers(), entry.node()))
              {
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
                // FIXME: handle local!
                if (auto r = std::dynamic_pointer_cast<Remote>(peer))
                {
                  auto lookup =
                    r->make_rpc<AddressSet (Address)>(
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
                  catch (elle::reactor::network::Error const& e)
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
          };
      }

      auto
      Kouncil::_lookup_node(Address id) const
        -> WeakMember
      {
        if (auto it = elle::find(this->_peers, id))
          return *it;
        else
          return Overlay::WeakMember();
      }

      void
      Kouncil::_perform(std::string const& name, std::function<void()> job)
      {
        this->_tasks.emplace_back(new elle::reactor::Thread(name, job));
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
      Kouncil::type_name() const
      {
        return "kouncil";
      }

      elle::json::Array
      Kouncil::peer_list() const
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
      Kouncil::stats() const
      {
        return
          {
            {"type", this->type_name()},
            {"id", elle::sprintf("%s", this->doughnut()->id())},
          };
      }

      void
      Kouncil::_notify_observers(PeerInfo const& pi)
      {
        if (!this->local())
          return;
        // FIXME: One Thread and one RPC to notify them all by batch, not one by
        // one.
        this->_perform("notify observers",
          [this, pi]
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
          ELLE_DUMP("peers: %f", this->_infos);
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
            auto infos = PeerInfos{};
            for (auto const& l: peers)
              infos.emplace(l.id(), l.endpoints(), -1);
            this->_discover(infos);
          }
          else
          {
            auto advertise =
              r.make_rpc<std::pair<PeerInfos, Configuration> (PeerInfos const&)>
              ("kouncil_advertise");
            auto res = advertise(send_peers ? this->_infos : PeerInfos());
            ELLE_TRACE("fetched %s peers", res.first.size());
            ELLE_DUMP("peers: %s", res.first);
            this->_discover(res.first);
            this->_infos.modify(
              ELLE_ENFORCE(elle::find(this->_infos, r.id())),
              [&] (auto& p) { p.storing(res.second.storing); });
          }
        }
        catch (elle::reactor::network::Error const& e)
        {
          ELLE_TRACE("%s: network exception advertising %s: %s", this, r, e);
          // nothing to do, disconnected() will be emited and handled.
        }
      }

      void
      Kouncil::_fetch_entries(Remote& r)
      {
        ELLE_TRACE_SCOPE("%f: fetch_entries of %f", this, r);
        auto fetch = r.make_rpc<AddressSet()>("kouncil_fetch_entries");
        auto entries = fetch();
        ELLE_ASSERT_NEQ(r.id(), Address::null);
        for (auto const& b: entries)
          this->_address_book.emplace(r.id(), b);
        ELLE_DEBUG("added %s entries from %f", entries.size(), r);
      }

      /*---------.
      | PeerInfo |
      `---------*/

      Kouncil::PeerInfo::PeerInfo(Address id,
                                  Endpoints endpoints,
                                  int64_t stamp,
                                  LamportAge d,
                                  boost::optional<bool> storing)
        : _id(id)
        , _endpoints(std::move(endpoints))
        , _stamp(stamp)
        , _disappearance(d)
        , _storing(storing)
      {}

      Kouncil::PeerInfo::PeerInfo(Address id,
                                  Endpoints endpoints,
                                  Time t,
                                  LamportAge d,
                                  boost::optional<bool> storing)
        : PeerInfo{id, endpoints, to_milliseconds(t), d, storing}
      {}

      Kouncil::PeerInfo::PeerInfo(NodeLocation const& loc)
        : PeerInfo{loc.id(), loc.endpoints()}
      {}

      bool
      Kouncil::PeerInfo::merge(Kouncil::PeerInfo const& from)
      {
        if ((this->_stamp < from._stamp || from._stamp == -1)
            && this->_endpoints != from._endpoints)
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
        elle::fprintf(o, "%f(%f)", elle::type_info(*this), this->id());
      }


      /*--------------.
      | StaleEndpoint |
      `--------------*/

      Kouncil::StaleEndpoint::StaleEndpoint(NodeLocation const& l)
        : NodeLocation(l)
        , _retry_timer(elle::reactor::scheduler().io_service())
        , _retry_counter(0)
        , _evict_timer(elle::reactor::scheduler().io_service())
      {}

      void
      Kouncil::StaleEndpoint::clear()
      {
        ELLE_TRACE("%f: clear", this);
        this->_slot.disconnect();
        this->_retry_counter = 0;
        this->_retry_timer.cancel();
        this->_evict_timer.cancel();
      }

      void
      Kouncil::StaleEndpoint::reconnect(Kouncil& kouncil)
      {
        {
          // The age so forth.
          elle::athena::LamportAge::Duration age;
          // If we're not in the info, we got disconnected before the
          // advertisement and the age is pretty much 0.
          if (auto info = elle::find(kouncil._infos, this->id()))
            age = info->disappearance().age();
          // How much it is still credited.
          auto respite = kouncil._eviction_delay - age;
          ELLE_DEBUG("%f: initiating eviction of %f with timeout %s",
                     kouncil, this, respite);
          this->_evict_timer.expires_from_now(respite);
          this->_evict_timer.async_wait(
            [this, &kouncil] (boost::system::error_code const& e)
            {
              if (e == boost::system::errc::operation_canceled)
                return;
              if (e)
                ELLE_ABORT("unexpected timer error: %s", e);
              ELLE_TRACE("%f: reconnection to %f timed out, evicting",
                         kouncil, id());
              kouncil._peer_evicted(id());
            });
        }
        // Initiate the reconnection attempts.
        this->connect(kouncil);
      }

      void
      Kouncil::StaleEndpoint::connect(Kouncil& kouncil)
      {
        ++this->_retry_counter;
        ELLE_DEBUG("%f: connection attempt #%s to %f", kouncil, this->_retry_counter, this);
        auto c = kouncil.doughnut()->dock().connect(*this);
        this->_slot
          = c->on_disconnection().connect([&]{ this->failed(kouncil); });
      }

      void
      Kouncil::StaleEndpoint::failed(Kouncil& kouncil)
      {
        auto d = std::chrono::seconds{1 << std::min(10, this->_retry_counter)};
        ELLE_DEBUG("%f: connection attempt #%s to %f failed, waiting %s before next",
                   kouncil, this->_retry_counter, this, d);
        this->_retry_timer.expires_from_now(d);
        this->_retry_timer.async_wait(
          [this, &kouncil] (boost::system::error_code const& e)
          {
            if (e == boost::system::errc::operation_canceled)
              return;
            if (e)
              ELLE_ABORT("unexpected timer error: %s", e);
            this->connect(kouncil);
          });
      }

      void
      Kouncil::StaleEndpoint::print(std::ostream& stream) const
      {
        elle::fprintf(stream, "%f(%f)", elle::type_info(*this), this->id());
      }

      namespace
      {
        const auto a
          = elle::TypeInfo::RegisterAbbrevation{"infinit::overlay::kouncil::Kouncil", "Kouncil"};
      }
    }
  }
}
