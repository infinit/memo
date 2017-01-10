#include <chrono>

#include <elle/log.hh>

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
      /*------.
      | Entry |
      `------*/

      Kouncil::Entry::Entry(model::Address node, model::Address block)
        : _node(std::move(node))
        , _block(std::move(block))
      {}

      /*-------------.
      | Construction |
      `-------------*/

      Kouncil::Kouncil(
        model::doughnut::Doughnut* dht,
        std::shared_ptr<infinit::model::doughnut::Local> local,
        boost::optional<int> eviction_delay)
        : Overlay(dht, local)
        , _address_book()
        , _peers()
        , _new_entries()
        , _broadcast_thread(new reactor::Thread(
                              elle::sprintf("%s: broadcast", this),
                              std::bind(&Kouncil::_broadcast, this)))
        , _watcher_thread(new reactor::Thread(
                          elle::sprintf("%s: watch", this),
                          std::bind(&Kouncil::_watcher, this)))
        , _eviction_delay(eviction_delay ? *eviction_delay : 12000)
      {
        using model::Address;
        ELLE_TRACE_SCOPE("%s: construct", this);
        if (local)
        {
          this->_peers.emplace(local);
          this->_infos.insert(std::make_pair(this->id(),
            PeerInfo {
              local->server_endpoints(),
              {},
              std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count(),
              std::chrono::high_resolution_clock::now(),
              std::chrono::high_resolution_clock::now()
            }));
          for (auto const& key: local->storage()->list())
            this->_address_book.emplace(this->id(), key);
          ELLE_DEBUG("loaded %s entries from storage",
                     this->_address_book.size());
          this->_connections.emplace_back(local->on_store().connect(
            [this] (model::blocks::Block const& b)
            {
              ELLE_DEBUG("%s(%f): registering new block %f",
                this, this->id(), b.address());
              this->_address_book.emplace(this->id(), b.address());
              std::unordered_set<Address> entries;
              entries.emplace(b.address());
              this->_new_entries.put(b.address());
            }));
          // Add server-side kouncil RPCs.
          local->on_connect().connect(
            [this] (RPCServer& rpcs)
            {
              // List all blocks owned by this node.
              rpcs.add(
                "kouncil_fetch_entries",
                std::function<std::unordered_set<Address> ()>(
                  [this] ()
                  {
                    std::unordered_set<Address> res;
                    auto range = this->_address_book.equal_range(this->id());
                    for (auto it = range.first; it != range.second; ++it)
                      res.emplace(it->block());
                    return res;
                  }));
              // Lookup owners of a block on this node.
              rpcs.add(
                "kouncil_lookup",
                std::function<std::unordered_set<Address>(Address)>(
                  [this] (Address const& addr)
                  {
                    std::unordered_set<Address> res;
                    auto range = this->_address_book.get<1>().equal_range(addr);
                    for (auto it = range.first; it != range.second; ++it)
                      res.emplace(it->node());
                    return res;
                  }));
              // Send known peers to this node and retrieve its known peers.
              if (this->doughnut()->version() < elle::Version(0, 8, 0))
                rpcs.add(
                  "kouncil_advertise",
                  std::function<NodeLocations(NodeLocations const&)>(
                    [this](NodeLocations const& peers)
                    {
                      this->_discover(peers);
                      return this->peers_locations();
                    }));
                else
                  rpcs.add(
                    "kouncil_advertise",
                    std::function<PeerInfos(PeerInfos const&)>(
                      [this](PeerInfos  const& infos)
                      {
                        this->_discover(infos);
                        return this->_infos;
                      }));
            });
        }
        // Add client-side Kouncil RPCs.
        this->doughnut()->dock().on_connect().connect(
          [this] (model::doughnut::Remote& r)
          {
            // Notify this node of new peers.
            if (this->doughnut()->version() < elle::Version(0, 8, 0))
              r.rpc_server().add(
                "kouncil_discover",
                std::function<void (NodeLocations const&)>(
                  [this] (NodeLocations const& nls)
                  {
                    this->_discover(nls);
                  }));
            else
              r.rpc_server().add(
                "kouncil_discover",
                std::function<void(PeerInfos const&)>(
                  [this](PeerInfos const& pis)
                  {
                    this->_discover(pis);
                  }));
            // Notify this node of new blocks owned by the peer.
            r.rpc_server().add(
              "kouncil_add_entries",
              std::function<void (std::unordered_set<Address> const&)>(
                [this, &r] (std::unordered_set<Address> const& entries)
                {
                  for (auto const& addr: entries)
                    this->_address_book.emplace(r.id(), addr);
                  ELLE_TRACE("%s: added %s entries from %f",
                             this, entries.size(), r.id());
                }));
          });
        this->_validate();
      }

      Kouncil::~Kouncil()
      {
        ELLE_TRACE("%s: destruct", this);
        this->_watcher_thread->terminate_now();
        this->_connections.clear();
      }

      NodeLocations
      Kouncil::peers_locations() const
      {
        NodeLocations res;
        for (auto const& p: this->peers())
        {
          if (auto r = dynamic_cast<model::doughnut::Remote const*>(p.get()))
            res.emplace_back(r->id(), r->endpoints());
          else if (auto l = dynamic_cast<model::doughnut::Local const*>(p.get()))
            res.emplace_back(p->id(), elle::unconst(l)->server_endpoints());
        }
        return res;
      }

      elle::json::Json
      Kouncil::query(std::string const& k, boost::optional<std::string> const& v)
      {
        elle::json::Object res;
        if (k == "stats")
        {
          res["peers"] = this->peer_list();
          res["id"] = elle::sprintf("%s", this->doughnut()->id());
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
          std::unordered_set<model::Address> entries;
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
        for (auto const& peer: peers)
        {
          this->_discover(PeerInfos{{
            peer.id(),
            PeerInfo{
              {},
              peer.endpoints(),
              0,
              std::chrono::high_resolution_clock::now(),
          }}});
        }
      }

      void
      Kouncil::_discover(PeerInfos::value_type const& p)
      {
        overlay::Overlay::Member peer;
        ELLE_DEBUG("connect to %f", p)
        {
          ELLE_ASSERT_NEQ(p.first, this->doughnut()->id());
          NodeLocation nl(p.first, p.second.endpoints_stamped);
          nl.endpoints().merge(p.second.endpoints_unstamped);
          try
          {
            peer = this->doughnut()->dock().make_peer(
              nl,
              model::EndpointsRefetcher([this] (model::Address id)
                                        {
                                          return this->_endpoints_refetch(id);
                                        }),
              [this, &p](overlay::Overlay::Member peer) {
                this->_connections.emplace_back(
                  peer->disconnected().connect([this, ptr=peer.get()] {
                      ELLE_TRACE("peer %s disconnected", ptr);
                      this->_peer_disconnected(ptr);
                  }));
                this->_connections.emplace_back(
                  peer->connected().connect(
                    [this,
                    peer = std::ambivalent_ptr<model::doughnut::Peer>::own(peer),
                    pi = std::make_pair(p.first, p.second)]
                    {
                      auto p = peer.lock();
                      elle::unconst(peer).payload().reset();
                      ELLE_ASSERT(p);
                      bool anon = pi.first == model::Address::null;
                      // We need next call to connect() after a reconnection to
                      // *not* be flagged anonymous
                      elle::unconst(pi).first = p->id(); // is that even legal?
                      this->_peer_connected(p, anon, pi.second);

                    }));
              }).lock();
          }
          catch (reactor::network::Exception const& e)
          {
            ELLE_TRACE("%s: network exception connecting to %s: %s", this, p, e);
            return;
          }
        }
        ELLE_DEBUG_SCOPE("broadcast new peer informations");
        // Broadcast this peer existence
        if (p.first != model::Address::null)
          this->_notify_observers(p);
      }

      void
      Kouncil::_peer_connected(overlay::Overlay::Member peer,
                               bool anonymous,
                               PeerInfo const& pi)
      {
        auto ptr = peer.get();
        ELLE_ASSERT_NEQ(peer->id(), model::Address::null);
        // If this was an anonymous peer, check it's not a duplicate.
        if (anonymous)
        {
          auto it = this->_infos.find(peer->id());
          if (it != this->_infos.end())
          {
            ELLE_DEBUG(
              "anonymous connection to known peer %f, merge endpoints",
              peer);
            it->second.merge(pi);
            // This connection can stick in the dock forever and cause trouble,
            // close it.
            if (auto* r = dynamic_cast<model::doughnut::Remote*>(peer.get()))
              r->disconnect();
            return;
          }
          else
          {
            ELLE_DEBUG(
              "register anonymous connection to %f endpoints", peer);
            this->_infos.insert(std::make_pair(peer->id(), pi));
            this->_notify_observers(std::make_pair(peer->id(), pi));
          }
        }
        ELLE_ASSERT(ptr);
        ELLE_TRACE("%f connected", peer);
        auto it = this->_disconnected_peers.get<1>().find(ptr);
        if (it != this->_disconnected_peers.get<1>().end())
        {
          // This is a reconnection
          ELLE_ASSERT(this->_peers.find(ptr->id()) == this->_peers.end());
          this->_peers.insert(*it);
          this->_disconnected_peers.get<1>().erase(it);
        }
        else
          // This is the initial connection
          this->_peers.emplace(peer);
        auto r = std::dynamic_pointer_cast<model::doughnut::Remote>(peer);
        ELLE_ASSERT(r);
        this->_advertise(*r);
        this->_fetch_entries(*r);
        // Invoke on_discover
        {
          Endpoints eps;
          auto& pi = this->_infos.at(peer->id());
          eps.merge(pi.endpoints_stamped);
          eps.merge(pi.endpoints_unstamped);
          NodeLocation nl(peer->id(), eps);
          this->on_discover()(nl, false);
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

      /*-------.
      | Lookup |
      `-------*/

      reactor::Generator<Overlay::WeakMember>
      Kouncil::_allocate(model::Address address, int n) const
      {
        using model::Address;
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
      Kouncil::_lookup(model::Address address, int n, bool) const
      {
        using model::Address;
        return reactor::generator<Overlay::WeakMember>(
          [this, address, n]
          (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
          {
            auto range = this->_address_book.get<1>().equal_range(address);
            int count = 0;
            for (auto it = range.first; it != range.second; ++it)
            {
              auto p = this->peers().find(it->node());
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
                    model::doughnut::Remote>(peer))
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
                    ELLE_DEBUG("skipping peer with network issue: %s (%s)", peer, e);
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
      Kouncil::_lookup_node(model::Address address) const
      {
        auto it = this->_peers.find(address);
        if (it != this->_peers.end())
        {
          ELLE_DEBUG("%s: node %s found in peers", this, address);
          return *it;
        }
        auto it2 = this->_disconnected_peers.find(address);
        if (it2 != this->_disconnected_peers.end())
        {
          ELLE_DEBUG("%s: node %s found in disconnected peers", this, address);
          return *it2;
        }
        ELLE_DEBUG("%s: node %s not found", this, address);
        return Overlay::WeakMember();
      }

      void
      Kouncil::_perform(std::string const& name, std::function<void()> job)
      {
        this->_tasks.emplace_back(new reactor::Thread(name, job));
        for (unsigned i=0; i<this->_tasks.size(); ++i)
        {
          if (!this->_tasks[i] || this->_tasks[i]->done())
          {
            std::swap(this->_tasks[i], this->_tasks[this->_tasks.size()-1]);
            this->_tasks.pop_back();
            --i;
          }
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
        elle::json::Array res;
        for (auto const& p: this->peers())
          if (auto r = dynamic_cast<model::doughnut::Remote const*>(p.get()))
          {
            elle::json::Array endpoints;
            for (auto const& e: r->endpoints())
              endpoints.push_back(elle::sprintf("%s", e));
            res.push_back(elle::json::Object{
              { "id", elle::sprintf("%x", r->id()) },
              { "endpoints",  endpoints },
              { "connected",  true},
            });
          }
        for (auto const& p: this->_disconnected_peers)
          if (auto r = dynamic_cast<model::doughnut::Remote const*>(p.get()))
          {
            elle::json::Array endpoints;
            for (auto const& e: r->endpoints())
              endpoints.push_back(elle::sprintf("%s", e));
            res.push_back(elle::json::Object{
              { "id", elle::sprintf("%x", r->id()) },
              { "endpoints",  endpoints },
              { "connected",  false},
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
      Kouncil::_peer_disconnected(model::doughnut::Peer* peer)
      {
        ELLE_TRACE_SCOPE("%s: %s disconnected", this, peer);
        auto it = this->_peers.find(peer->id());
        if (it == this->_peers.end() || it->get() != peer)
        {
          ELLE_DEBUG("disconnect on dropped pear %s", peer);
          return;
        }
        auto& pi = this->_infos.at(peer->id());
        pi.last_seen = std::chrono::high_resolution_clock::now();
        pi.last_contact_attempt = pi.last_seen;
        this->_disconnected_peers.insert(*it);
        auto its = this->_address_book.equal_range(peer->id());
        this->_address_book.erase(its.first, its.second);
        this->_peers.erase(it);
      }

      void
      Kouncil::_discover(PeerInfos const& pis)
      {
        for (auto const& pi: pis)
        {
          if (pi.first == model::Address::null)
          {
            ELLE_DEBUG("discovering anonymous peer %s", pi);
            this->_perform("connect",
              [this, pi = pi] {
              this->_discover(pi);});
          }
          else
          {
            auto it = this->_infos.find(pi.first);
            if (it == this->_infos.end())
            {
              ELLE_DEBUG("discovering named peer %s", pi);
              this->_infos.insert(pi);
              this->_perform(
                "connect",
                [this, pi = *this->_infos.find(pi.first) /* FIXME: that's just pi ?? */]
                {
                  this->_discover(pi);
                });
            }
            else
            {
              ELLE_DEBUG("discovering known peer %s", pi);
              if (it->second.merge(pi.second))
              {
                // New data on a connected peer, we need to notify observers
                // FIXME: maybe notify on reconnection instead?
                ELLE_DEBUG("broadcast new endpoints for peer %s", pi);
                this->_notify_observers(*it);
              }
            }
          }
        }
      }

      void
      Kouncil::_notify_observers(PeerInfos::value_type const& pi)
      {
        if (!this->local())
          return;
        this->_perform("notify observers",
          [this, pi=pi]
          {
            try
            {
              ELLE_DEBUG("%s: notifying observers of %s", this, pi);
              if (this->doughnut()->version() < elle::Version(0, 8, 0))
              {
                NodeLocation nl(pi.first, pi.second.endpoints_stamped);
                nl.endpoints().merge(pi.second.endpoints_unstamped);
                this->local()->broadcast<void>("kouncil_discover",
                                               NodeLocations{nl});
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
      Kouncil::_advertise(model::doughnut::Remote& r)
      {
        ELLE_TRACE_SCOPE("fetch know peers of %s", r);
        try
        {
          if (this->doughnut()->version() < elle::Version(0, 8, 0))
          {
            auto advertise = r.make_rpc<NodeLocations (NodeLocations const&)>(
              "kouncil_advertise");
            auto peers = advertise(this->peers_locations());
            ELLE_TRACE("fetched %s peers", peers.size());
            ELLE_DUMP("peers: %s", peers);
            // FIXME: might be useless to broadcast these peers
            this->_discover(peers);
          }
          else
          {
            auto reg = r.make_rpc<PeerInfos(PeerInfos const&)>("kouncil_advertise");
            auto npi = reg(this->_infos);
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
      Kouncil::_fetch_entries(model::doughnut::Remote& r)
      {
        auto fetch = r.make_rpc<std::unordered_set<model::Address> ()>(
          "kouncil_fetch_entries");
        auto entries = fetch();
        ELLE_ASSERT_NEQ(r.id(), model::Address::null);
        for (auto const& b: entries)
          this->_address_book.emplace(r.id(), b);
        ELLE_DEBUG("added %s entries from %f", entries.size(), r);
      }

      boost::optional<Endpoints>
      Kouncil::_endpoints_refetch(model::Address id)
      {

        auto it = this->_infos.find(id);
        if (it != this->_infos.end())
        {
          Endpoints res;
          res.merge(it->second.endpoints_stamped);
          res.merge(it->second.endpoints_unstamped);
          ELLE_DEBUG("updating endpoints for %s with %s entries",
                     id, res.size());
          return res;
        }
        return boost::none;
      }

      bool
      Kouncil::PeerInfo::merge(Kouncil::PeerInfo const& from)
      {
        bool changed = false;
        if (this->stamp < from.stamp)
        {
          this->endpoints_stamped = from.endpoints_stamped;
          this->stamp = from.stamp;
          changed = true;
        }
        if (this->endpoints_unstamped.merge(from.endpoints_unstamped))
          changed = true;
        return changed;
      }

      void
      Kouncil::_watcher()
      {
        static auto const retry_max_interval =
          elle::chrono::duration_parse<std::milli>(
            elle::os::getenv("INFINIT_KOUNCIL_WATCHER_MAX_RETRY", "60"));
        while (true)
        {
          auto now = std::chrono::high_resolution_clock::now();
          auto it = this->_disconnected_peers.begin();
          while (it != this->_disconnected_peers.end())
          {
            auto id = (*it)->id();
            auto& info = this->_infos.at(id);
            if (now - info.last_seen > std::chrono::seconds(this->_eviction_delay))
            {
              ELLE_TRACE("%s: evicting %s", this, *it);
              it = this->_disconnected_peers.erase(it);
              this->_infos.erase(id);
              this->on_disappear()(id, false);
              continue;
            }
            if ((now - info.last_seen) / 2 < now - info.last_contact_attempt
                || now - info.last_contact_attempt > retry_max_interval)
            {
              ELLE_TRACE("%s: attempting to contact %s", this, *it);
              info.last_contact_attempt = now;
              // try contacting this node again
              this->_perform(
                "ping",
                [peer = *it]
                {
                  try
                  {
                    peer->fetch(model::Address::random(), boost::none);
                  }
                  catch (elle::Error const& e)
                  {}
                });
            }
            ++it;
          }
          static auto const sleep_time =
            elle::chrono::duration_parse<std::milli>(
              elle::os::getenv("INFINIT_KOUNCIL_WATCHER_INTERVAL", "10"));
          reactor::sleep(boost::posix_time::milliseconds(sleep_time.count()));
        }
      }
    }
  }
}
