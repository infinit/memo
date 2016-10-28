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
        std::shared_ptr<infinit::model::doughnut::Local> local)
        : Overlay(dht, local)
        , _address_book()
        , _peers()
        , _new_entries()
        , _broadcast_thread(new reactor::Thread(
                              elle::sprintf("%s: broadcast", this),
                              std::bind(&Kouncil::_broadcast, this)))
      {
        using model::Address;
        ELLE_TRACE_SCOPE("%s: construct", this);
        if (local)
        {
          this->_peers.emplace(this->id(), local);
          for (auto const& key: local->storage()->list())
            this->_address_book.emplace(this->id(), key);
          ELLE_DEBUG("loaded %s entries from storage",
                     this->_address_book.size());
          this->_connections.emplace_back(local->on_store().connect(
            [this] (model::blocks::Block const& b)
            {
              this->_address_book.emplace(this->id(), b.address());
              std::unordered_set<Address> entries;
              entries.emplace(b.address());
              this->_new_entries.put(b.address());
            }));
          local->on_connect().connect(
            [this] (RPCServer& rpcs)
            {
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
            });
          local->on_connect().connect(
            [this] (RPCServer& rpcs)
            {
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
            });
          local->on_connect().connect(
            [this] (RPCServer& rpcs)
            {
              rpcs.add(
                "kouncil_advertise",
                std::function<NodeLocations(NodeLocations const&)>(
                  [this](NodeLocations const& nls)
                  {
                    ELLE_DEBUG("%s: receive advertise of %s", this, nls);
                    auto cp = this->_pending;
                    for (auto const& nl: nls)
                      this->_pending.insert(std::make_pair(nl.id(), nl.endpoints()));
                    // Discover must be called asynchronously or deadlock
                    for (auto const& nl: nls)
                      if (this->peers().find(nl.id()) == this->peers().end())
                        new reactor::Thread("discover", [this, nl] { this->_discover({nl});}, true);
                    return this->peers_locations(cp);
                  }));
            });
        }
        this->doughnut()->dock().on_connect().connect(
          [this] (model::doughnut::Remote& r)
          {
            // back-channel used by nodes to notify their connected observers
            // of new peers
            r.rpc_server().add(
                "kouncil_discover",
                std::function<void(NodeLocations const&)>(
                  [this](NodeLocations const& nls)
                  {
                    ELLE_DEBUG("%s: receive discover of %s", this, nls);
                    if (this->local())
                      return; // only observers need that back-channel
                    // Discover must be called asynchronously or deadlock
                    for (auto const& nl: nls)
                      if (this->peers().find(nl.id()) == this->peers().end())
                        new reactor::Thread("discover", [this, nl] { this->_discover({nl});}, true);
                  }));

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

            auto reg = r.make_rpc<NodeLocations(NodeLocations const&)>("kouncil_advertise");
            auto nls = this->peers_locations(this->_pending);
            ELLE_TRACE("%s: invoking advertise on %s with info on %s nodes", this, r, nls.size());
            auto rnls = reg(nls);
            ELLE_TRACE("%s: got info on %s nodes", this, rnls.size());
            for (auto const& nl: rnls)
              if (this->peers().find(nl.id()) == this->peers().end())
                new reactor::Thread("discover", [this, nl] { this->_discover({nl});}, true);
          });
        this->_validate();
      }

      Kouncil::~Kouncil()
      {
        ELLE_TRACE("%s: destruct", this);
        this->_connections.clear();
      }

      NodeLocations
      Kouncil::peers_locations(Pending const& extras) const
      {
        NodeLocations nls;
        for (auto const& p: this->peers())
        {
          if (auto r = dynamic_cast<model::doughnut::Remote const*>(p.second.get()))
            nls.emplace_back(r->id(), r->endpoints());
          else if (auto l = dynamic_cast<model::doughnut::Local const*>(p.second.get()))
            nls.emplace_back(p.second->id(), elle::unconst(l)->server_endpoints() );
        }
        for (auto const& p: extras)
          nls.emplace_back(p.first, p.second);
        return nls;
      }

      elle::json::Json
      Kouncil::query(std::string const& k, boost::optional<std::string> const& v)
      {
        elle::json::Object res;
        if (k == "stats")
        {
          elle::json::Array jpeers;
          for (auto const& p: peers())
            jpeers.push_back(elle::sprintf("%s", p.first));
          res["peers"] = jpeers;
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
          auto addr = this->_new_entries.get();
          ELLE_TRACE("%s: broadcast new entry: %f", this, addr);
          // FIXME: squash
          std::unordered_set<model::Address> entries = {addr};
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
        // FIXME: parallelize
        for (auto const& loc: peers)
        {
          auto it = this->_peers.find(loc.id());
          // skip if no new information
          if (it != this->_peers.end())
          {
            if (loc.endpoints().empty())
              continue;
            auto r = dynamic_cast<model::doughnut::Remote*>(it->second.get());
            if (!r)
              continue; // discover on an already Local o_O
            if (loc.endpoints().front().port() == r->endpoints().front().port())
              continue;
          }
          auto peer = this->doughnut()->dock().make_peer(
            loc, model::EndpointsRefetcher()).lock();
          ELLE_ASSERT(peer);
          this->_discover(std::move(peer));
        }
      }

      void
      Kouncil::_discover(Overlay::Member peer)
      {
        if (peer->id() == this->doughnut()->id())
        {
          ELLE_DEBUG("%s: _discover on known peer %s", this, peer->id());
          return;
        }
        // Don't process twice the same id at the same time
        if (this->_discovering.find(peer->id()) != this->_discovering.end())
        {
          ELLE_DEBUG("%s: already processing %s", this, peer->id());
          return;
        }
        this->_discovering.insert(peer->id());
        elle::SafeFinally remove_from_discovering([this, id=peer->id()] {
            this->_discovering.erase(id);
        });
        ELLE_DEBUG("%s: discovered %s", this, peer->id());
        // FIXME: handle local !
        if (auto r = std::dynamic_pointer_cast<model::doughnut::Remote>(peer))
        {
          auto fetch = r->make_rpc<std::unordered_set<model::Address> ()>(
            "kouncil_fetch_entries");
          auto entries = fetch();
          ELLE_ASSERT_NEQ(peer->id(), model::Address::null);
          for (auto const& b: entries)
            this->_address_book.emplace(peer->id(), b);
          ELLE_DEBUG("added %s entries from %f", entries.size(), peer);
        }
        ELLE_ASSERT_NEQ(peer->id(), model::Address::null);
        this->_peers.emplace(peer->id(), peer);
        this->_pending.erase(peer->id());
        ELLE_DEBUG("%s: notifying connections", this);
        if (this->local())
        {
          // we need to notify observers who are connected to us, as they
          // are not part of the _peer list
          if (auto r = dynamic_cast<model::doughnut::Remote const*>(peer.get()))
          {
            auto cpeers = this->local()->peers();
            for (auto cc: cpeers)
            {
              try
              {
                auto& c = elle::unconst(cc);
                RPC<void(NodeLocations const&)>rpc(
                  "kouncil_discover",
                  elle::unconst(c->channels()),
                  this->doughnut()->version(),
                  c->rpcs()._key);
                NodeLocation nl(peer->id(), r->endpoints());
                rpc(NodeLocations{nl});
              }
              catch (elle::Error const& e)
              {
                ELLE_TRACE("Exception invoking discover: %s", e);
              }
            }
          }
        }
        ELLE_DEBUG("%s: exiting discover", this);
      }

      template<typename E>
      std::vector<int>
      pick_n(E& gen, int size, int count)
      {
        std::vector<int> res;
        std::uniform_int_distribution<> random(0, size-1);
        while (res.size() < static_cast<unsigned int>(count))
        {
          int v = random(gen);
          if (std::find(res.begin(), res.end(), v) != res.end())
            continue;
          res.push_back(v);
        }
        return res;
      }

      /*-------.
      | Lookup |
      `-------*/

      reactor::Generator<Overlay::WeakMember>
      Kouncil::_lookup(model::Address address, int n, Operation op) const
      {
        using model::Address;
        if (op == Operation::OP_INSERT)
          return reactor::generator<Overlay::WeakMember>(
            [this, address, n]
            (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
            {
              ELLE_DEBUG("%s: selecting %s nodes from %s peers",
                         this, n, this->_peers.size());
              int count = 0;
              if (static_cast<unsigned int>(n) >= this->_peers.size())
                for (auto it = this->_peers.begin();
                     it != this->_peers.end() && count < n;
                     ++it, ++count)
                  yield(it->second);
              else if (this->_peers.size() >= static_cast<unsigned int>(n) * 2)
              { // picking a 'small' subset: retry on collision
                std::vector<int> indexes = pick_n(
                  elle::unconst(this)->_gen,
                  static_cast<int>(this->_peers.size()),
                  n);
                std::sort(indexes.begin(), indexes.end());
                unsigned int ii=0;
                int ip=0;
                for (auto& p: this->_peers)
                {
                  if (indexes[ii] == ip)
                  {
                    yield(p.second);
                    ++ii;
                    if (ii >= indexes.size())
                      break;
                  }
                  ++ip;
                }
              }
              else
              { // picking a large subset: take all and remove
                std::vector<int> indexes = pick_n(
                  elle::unconst(this)->_gen,
                  this->_peers.size(),
                  this->_peers.size()-n);
                std::sort(indexes.begin(), indexes.end());
                unsigned int ii=0;
                int ip=0;
                for (auto& p: this->_peers)
                {
                  if (ii < indexes.size() && indexes[ii] == ip)
                  { // skip it
                    ++ii;
                    continue;
                  }
                  yield(p.second);
                  ++ip;
                }
              }
            });
        else
          return reactor::generator<Overlay::WeakMember>(
            [this, address, n]
            (reactor::Generator<Overlay::WeakMember>::yielder const& yield)
            {
              auto range = this->_address_book.get<1>().equal_range(address);
              int count = 0;
              for (auto it = range.first; it != range.second; ++it)
              {
                yield(this->_peers.at(it->node()));
                if (++count >= n)
                  break;
              }
              if (count == 0)
              {
                ELLE_TRACE_SCOPE("%s: block %f not found, checking all peers",
                                 this, address);
                for (auto peer: this->peers())
                {
                  // FIXME: handle local !
                  if (auto r = std::dynamic_pointer_cast<model::doughnut::Remote>(peer.second))
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
          return it->second;
        else
          return Overlay::WeakMember();
      }
    }
  }
}
