#include <infinit/model/doughnut/consensus/Paxos.hh>

#include <functional>

#include <elle/memory.hh>
#include <elle/bench.hh>

#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/hash.hh>

#include <infinit/RPC.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/DummyPeer.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Paxos");

#define BENCH(name)                                      \
  static elle::Bench bench("bench.paxos." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {

        template<typename F>
        auto network_exception_to_unavailable(F f) -> decltype(f())
        {
          try
          {
            return f();
          }
          catch(reactor::network::Exception const& e)
          {
            ELLE_TRACE("network exception in paxos: %s", e);
            throw Paxos::PaxosClient::Peer::Unavailable();
          }
        }

        static
        Address
        uid(cryptography::rsa::PublicKey const& key)
        {
          auto serial = cryptography::rsa::publickey::der::encode(key);
          return
            cryptography::hash(serial, cryptography::Oneway::sha256).contents();
        }

        template <typename T>
        static
        void
        null_deleter(T*)
        {}

        BlockOrPaxos::BlockOrPaxos(blocks::Block& b)
          : block(&b, [] (blocks::Block*) {})
          , paxos()
        {}

        BlockOrPaxos::BlockOrPaxos(Paxos::LocalPeer::Decision* p)
          : block(nullptr)
          , paxos(p, [] (Paxos::LocalPeer::Decision*) {})
        {}

        BlockOrPaxos::BlockOrPaxos(elle::serialization::SerializerIn& s)
          : block(nullptr,
                  [] (blocks::Block* p)
                  {
                    std::default_delete<blocks::Block>()(p);
                  })
          , paxos(nullptr,
                  [] (Paxos::LocalPeer::Decision* p)
                  {
                    std::default_delete<Paxos::LocalPeer::Decision>()(p);
                  })
        {
          this->serialize(s);
        }

        void
        BlockOrPaxos::serialize(elle::serialization::Serializer& s)
        {
          s.serialize("block", this->block);
          s.serialize("paxos", this->paxos);
        }

        /*-------------.
        | Construction |
        `-------------*/

        Paxos::Paxos(Doughnut& doughnut,
                     int factor,
                     bool lenient_fetch,
                     bool rebalance_auto_expand,
                     std::chrono::system_clock::duration node_timeout)
          : Super(doughnut)
          , _factor(factor)
          , _lenient_fetch(lenient_fetch)
          , _rebalance_auto_expand(rebalance_auto_expand)
          , _node_timeout(node_timeout)
        {
          if (getenv("INFINIT_PAXOS_LENIENT_FETCH"))
            _lenient_fetch = true;
        }

        /*--------.
        | Factory |
        `--------*/

        std::unique_ptr<Local>
        Paxos::make_local(boost::optional<int> port,
                          std::unique_ptr<storage::Storage> storage)
        {
          return elle::make_unique<consensus::Paxos::LocalPeer>(
            *this,
            this->factor(),
            this->_rebalance_auto_expand,
            this->_node_timeout,
            this->doughnut(),
            this->doughnut().id(),
            std::move(storage),
            port ? port.get() : 0);
        }

        /*-----.
        | Peer |
        `-----*/

        class Peer
          : public Paxos::PaxosClient::Peer
        {
        public:
          Peer(overlay::Overlay::WeakMember member,
               Address address,
               boost::optional<int> local_version = {})
            : Paxos::PaxosClient::Peer((ELLE_ASSERT(member.lock()),
                                        member.lock()->id()))
            , _member(std::move(member))
            , _address(address)
            , _local_version(local_version)
          {}

          overlay::Overlay::Member
          _lock_member()
          {
            auto member = this->_member.lock();
            if (!member)
            {
              ELLE_WARN("%s: peer %f was deleted", this, this->id());
              throw Paxos::PaxosClient::Peer::Unavailable();
            }
            return member;
          }

          virtual
          boost::optional<Paxos::PaxosClient::Accepted>
          propose(Paxos::PaxosClient::Quorum const& q,
                  Paxos::PaxosClient::Proposal const& p) override
          {
            BENCH("propose");
            auto member = this->_lock_member();
            return network_exception_to_unavailable([&] {
              if (auto local =
                  dynamic_cast<Paxos::LocalPeer*>(member.get()))
                return local->propose(
                  q, this->_address, p);
              else if (auto remote =
                       dynamic_cast<Paxos::RemotePeer*>(member.get()))
                return remote->propose(
                  q, this->_address, p);
              else if (dynamic_cast<DummyPeer*>(member.get()))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", member);
            });
          }

          virtual
          Paxos::PaxosClient::Proposal
          accept(Paxos::PaxosClient::Quorum const& q,
                 Paxos::PaxosClient::Proposal const& p,
                 Paxos::Value const& value) override
          {
            BENCH("accept");
            auto member = this->_lock_member();
            return network_exception_to_unavailable([&] {
              if (auto local =
                  dynamic_cast<Paxos::LocalPeer*>(member.get()))
                return local->accept(
                  q, this->_address, p, value);
              else if (auto remote =
                       dynamic_cast<Paxos::RemotePeer*>(member.get()))
                {
                  if (value.is<std::shared_ptr<blocks::Block>>()
                      || remote->doughnut().version() >= elle::Version(0, 5, 0))
                    return remote->accept(
                      q, this->_address, p, value);
                  else
                  {
                    ELLE_TRACE("unmanageable accept on non-block value");
                    throw reactor::network::Exception("Peer unavailable");
                  }
                }
              else if (dynamic_cast<DummyPeer*>(member.get()))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", member);
            });
          }

          virtual
          void
          confirm(Paxos::PaxosClient::Quorum const& q,
                  Paxos::PaxosClient::Proposal const& p) override
          {
            BENCH("confirm");
            auto member = this->_lock_member();
            return network_exception_to_unavailable([&] {
              if (auto local =
                  dynamic_cast<Paxos::LocalPeer*>(member.get()))
              {
                if (local->doughnut().version() >= elle::Version(0, 5, 0))
                  local->confirm(q, this->_address, p);
                return;
              }
              else if (auto remote =
                       dynamic_cast<Paxos::RemotePeer*>(member.get()))
              {
                if (remote->doughnut().version() >= elle::Version(0, 5, 0))
                  remote->confirm(q, this->_address, p);
                return;
              }
              else if (dynamic_cast<DummyPeer*>(member.get()))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", member);
            });
          }

          virtual
          boost::optional<Paxos::PaxosClient::Accepted>
          get(Paxos::PaxosClient::Quorum const& q) override
          {
            BENCH("get");
            auto member = this->_lock_member();
            return network_exception_to_unavailable([&] {
              if (auto local =
                  dynamic_cast<Paxos::LocalPeer*>(member.get()))
                return local->get(q, this->_address, this->_local_version);
              else if (auto remote =
                       dynamic_cast<Paxos::RemotePeer*>(member.get()))
                return remote->get(q, this->_address, this->_local_version);
              else if (dynamic_cast<DummyPeer*>(member.get()))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", member);
            });
          }

          ELLE_ATTRIBUTE_R(overlay::Overlay::WeakMember, member);
          ELLE_ATTRIBUTE(Address, address);
          ELLE_ATTRIBUTE(boost::optional<int>, local_version);
        };

        static
        Paxos::PaxosClient::Peers
        lookup_nodes(Doughnut& dht,
                     Paxos::PaxosServer::Quorum const& q,
                     Address address)
        {
          Paxos::PaxosClient::Peers res;
          for (auto member: dht.overlay()->lookup_nodes(q))
            res.push_back(
              elle::make_unique<Peer>(std::move(member), address));
          return res;
        }

        /*-----------.
        | RemotePeer |
        `-----------*/

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::RemotePeer::propose(PaxosServer::Quorum const& peers,
                                   Address address,
                                   PaxosClient::Proposal const& p)
        {
          return network_exception_to_unavailable([&] {
            auto propose = make_rpc<boost::optional<PaxosClient::Accepted>(
              PaxosServer::Quorum,
              Address,
              PaxosClient::Proposal const&)>("propose");
            propose.set_context<Doughnut*>(&this->_doughnut);
            return propose(peers, address, p);
          });
        }

        Paxos::PaxosClient::Proposal
        Paxos::RemotePeer::accept(PaxosServer::Quorum const& peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p,
                                  Value const& value)
        {
          return network_exception_to_unavailable([&] {
            if (this->doughnut().version() < elle::Version(0, 5, 0))
            {
              ELLE_ASSERT(value.is<std::shared_ptr<blocks::Block>>());
              auto accept = make_rpc<Paxos::PaxosClient::Proposal (
                PaxosServer::Quorum peers,
                Address,
                Paxos::PaxosClient::Proposal const&,
                std::shared_ptr<blocks::Block>)>("accept");
              accept.set_context<Doughnut*>(&this->_doughnut);
              return accept(peers, address, p, value.get<std::shared_ptr<blocks::Block>>());
            }
            else
            {
              auto accept = make_rpc<Paxos::PaxosClient::Proposal (
                PaxosServer::Quorum peers,
                Address,
                Paxos::PaxosClient::Proposal const&,
                Value const&)>("accept");
              accept.set_context<Doughnut*>(&this->_doughnut);
              return accept(peers, address, p, value);
            }
          });
        }

        void
        Paxos::RemotePeer::confirm(PaxosServer::Quorum const& peers,
                                   Address address,
                                   PaxosClient::Proposal const& p)
        {
          return network_exception_to_unavailable([&] {
            auto confirm = make_rpc<void(
              PaxosServer::Quorum,
              Address,
              PaxosClient::Proposal const&)>("confirm");
            confirm.set_context<Doughnut*>(&this->_doughnut);
            return confirm(peers, address, p);
          });
        }

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::RemotePeer::get(PaxosServer::Quorum const& peers,
                               Address address,
                               boost::optional<int> local_version)
        {
          return network_exception_to_unavailable([&] {
            auto get = make_rpc<boost::optional<PaxosClient::Accepted>(
              PaxosServer::Quorum,
              Address, boost::optional<int>)>("get");
            get.set_context<Doughnut*>(&this->_doughnut);
            return get(peers, address, local_version);
          });
        }

        /*----------.
        | LocalPeer |
        `----------*/

        Paxos::LocalPeer::~LocalPeer()
        {
          ELLE_TRACE_SCOPE("%s: destruct", *this);
          this->_rebalance_thread.terminate_now();
        }

        void
        Paxos::LocalPeer::initialize()
        {
          this->doughnut().overlay()->on_discover().connect(
            [this] (Address id, bool observer)
            {
              if (!observer)
                this->_discovered(id);
            });
          this->doughnut().overlay()->on_disappear().connect(
            [this] (Address id, bool observer)
            {
              if (!observer)
                this->_disappeared(id);
            });
          if (this->_factor > 1)
            this->_rebalance_inspector.reset(
              new reactor::Thread(
                elle::sprintf("%s: rebalancing inspector", this),
                [this]
                {
                  try
                  {
                    ELLE_TRACE_SCOPE("%s: inspect disk blocks for rebalancing",
                                     this);
                    for (auto address: this->storage()->list())
                    {
                      reactor::sleep(100_ms);
                      try
                      {
                        this->_load(address);
                        auto quorum = this->_quorums.find(address);
                        ELLE_ASSERT(quorum != this->_quorums.end());
                        if (quorum->replication_factor() >= this->_factor)
                          this->_addresses.erase(address);
                      }
                      catch (MissingBlock const&)
                      {}
                    }
                  }
                  catch (elle::Error const& e)
                  {
                    ELLE_ERR("disk rebalancer inspector exited: %s", e);
                  }
                }));
        }

        void
        Paxos::LocalPeer::cleanup()
        {
          this->_rebalance_inspector.reset();
          this->_rebalance_thread.terminate_now();
        }

        Paxos::LocalPeer::Decision&
        Paxos::LocalPeer::_load(Address address,
                                boost::optional<PaxosServer::Quorum> peers)
        {
          auto decision = this->_addresses.find(address);
          if (decision != this->_addresses.end())
            return decision->second;
          else
            try
            {
              ELLE_TRACE_SCOPE("%s: load paxos %f from storage",
                               *this, address);
              auto buffer = this->storage()->get(address);
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto stored =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  buffer, true, context);
              if (!stored.paxos)
                // FIXME: this will trigger the retry with a mutable block
                // type in Consensus::fetch
                throw MissingBlock(address);
              return this->_load(address, std::move(*stored.paxos));
            }
            catch (storage::MissingKey const& e)
            {
              ELLE_TRACE("%s: missingkey reloading decision", *this);
              if (peers)
              {
                auto version =
                  elle_serialization_version(this->doughnut().version());
                return this->_load(
                  address, Decision(PaxosServer(this->id(), *peers, version)));
              }
              else
                throw MissingBlock(e.key());
            }
        }

        Paxos::LocalPeer::Decision&
        Paxos::LocalPeer::_load(Address address,
                                Paxos::LocalPeer::Decision decision)
        {
          auto const& quorum = decision.paxos.current_quorum();
          this->_cache(address, quorum);
          if (this->_rebalance_auto_expand &&
              decision.paxos.current_value() &&
              signed(quorum.size()) < this->_factor)
            this->_rebalancable.put(std::make_pair(address, false));
          return this->_addresses.emplace(
            address, std::move(decision)).first->second;
        }

        void
        Paxos::LocalPeer::_cache(Address address, Quorum quorum)
        {
          this->_quorums.erase(address);
          for (auto const& node: quorum)
            this->_node_blocks[node].erase(address);
          this->_quorums.emplace(address, quorum);
          for (auto const& node: quorum)
            this->_node_blocks[node].insert(address);
        }

        void
        Paxos::LocalPeer::_discovered(model::Address id)
        {
          this->_nodes.emplace(id);
          this->_node_timeouts.erase(id);
          if (this->_rebalance_auto_expand)
            this->_rebalancable.put(std::make_pair(id, true));
        }

        void
        Paxos::LocalPeer::_disappeared(model::Address id)
        {
          if (this->_nodes.erase(id))
          {
            ELLE_TRACE("%s: node %f disappeared, evict in %s",
                       this, id, this->_node_timeout);
            this->_disappeared_schedule_eviction(id);
          }
        }

        void
        Paxos::LocalPeer::_disappeared_schedule_eviction(model::Address id)
        {
          auto it = this->_node_timeouts.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(id),
            std::forward_as_tuple(reactor::scheduler().io_service()));
          it.first->second.cancel();
          it.first->second.expires_from_now(
            boost::posix_time::seconds(
              this->_node_timeout.count() *
              decltype(this->_node_timeout)::period::num /
              decltype(this->_node_timeout)::period::den));
          it.first->second.async_wait(
            [this, id] (const boost::system::error_code& error)
            {
              if (!error)
                this->_evict_threads.emplace_back(
                  new reactor::Thread(
                    elle::sprintf("%s: evict %f", this, id),
                    [this, id]
                    {
                      ELLE_WARN("lost contact with %f for %s, evict",
                                id, this->_node_timeout);
                      this->_disappeared_evict(id);
                    }));
            });
        }

        void
        Paxos::LocalPeer::_disappeared_evict(model::Address lost_id)
        {
          auto blocks = this->_node_blocks.find(lost_id);
          if (blocks != this->_node_blocks.end())
            for (auto address: blocks->second)
            {
              ELLE_TRACE_SCOPE("%s: evict %f from %f quorum",
                               this, lost_id, address);
              auto& decision = this->_load(address);
              auto q = decision.paxos.current_quorum();
              Paxos::PaxosClient client(
                this->doughnut().id(),
                lookup_nodes(this->_paxos.doughnut(), q, address));
              if (q.erase(lost_id))
              {
                client.choose(decision.paxos.current_version() + 1, q);
                ELLE_TRACE("%s: evicted %f from %f quorum",
                           this, lost_id, address);
                if (signed(q.size()) < this->_factor)
                  this->_rebalancable.put(std::make_pair(address, false));
              }
            }
        }

        void
        Paxos::LocalPeer::_rebalance()
        {
          auto propagate = [this] (PaxosServer& paxos,
                                   Address a,
                                   PaxosServer::Quorum q)
            {
              if (auto value = paxos.current_value())
                ELLE_DEBUG("propagate block value")
                {
                  PaxosClient c(
                    this->doughnut().id(),
                    lookup_nodes(this->doughnut(), q, a));
                  // FIXME: do something in case of conflict
                  c.choose(
                    paxos.current_version() + 1,
                    value->value.get<std::shared_ptr<blocks::Block>>());
                }
              this->_rebalanced(a);
            };
          while (true)
          {
            auto elt = this->_rebalancable.get();
            if (!elt.second)
            {
              try
              {
                ELLE_TRACE_SCOPE("%s: rebalance block %f", this, elt.first);
                auto it = this->_addresses.find(elt.first);
                if (it == this->_addresses.end())
                  // The block was deleted in the meantime.
                  continue;
                auto peers = lookup_nodes(this->_paxos.doughnut(),
                                          it->second.paxos.current_quorum(),
                                          it->first);
                Paxos::PaxosClient client(
                  this->doughnut().id(), std::move(peers));
                if (this->_paxos._rebalance(client, elt.first))
                {
                  auto it = this->_addresses.find(elt.first);
                  if (it == this->_addresses.end())
                    // The block was deleted in the meantime.
                    continue;
                  auto q = it->second.paxos.current_quorum();
                  this->_quorums.modify(
                    this->_quorums.find(elt.first),
                    [&] (BlockRepartition& r) {r.quorum = q;});
                  for (auto const& node: q)
                    this->_node_blocks[node].insert(elt.first);
                  propagate(it->second.paxos, elt.first, q);
                }
              }
              catch (elle::Error const& e)
              {
                ELLE_WARN("rebalancing of %f failed: %s", elt.first, e);
              }
            }
            else
            {
              auto test = [&] (PaxosServer::Quorum const& q)
                {
                  return signed(q.size()) < this->_factor &&
                  q.find(elt.first) == q.end();
                };
              std::unordered_set<Address> targets;
              for (auto const& r: this->_quorums.get<1>())
              {
                if (r.replication_factor() >= this->_factor)
                  break;
                if (test(r.quorum))
                  targets.emplace(r.address);
              }
              if (targets.empty())
                continue;
              ELLE_TRACE_SCOPE(
                "%s: rebalance %s blocks to newly discovered peer %f",
                this, targets.size(), elt.first);
              for (auto address: targets)
                try
                {
                  auto it = this->_addresses.find(address);
                  if (it == this->_addresses.end())
                    // The block was deleted in the meantime.
                    continue;
                  // Beware of interators invalidation, use a reference.
                  auto& paxos = it->second.paxos;
                  auto quorum = paxos.current_quorum();
                  // We can't actually rebalance this block, under_represented
                  // was wrong. Don't think this can happen but better safe than
                  // sorry.
                  if (!test(quorum))
                    continue;
                  ELLE_DEBUG("elect new quorum")
                  {
                    PaxosClient c(
                    this->doughnut().id(),
                    lookup_nodes(this->doughnut(), quorum, address));
                    quorum.insert(elt.first);
                    // FIXME: do something in case of conflict
                    c.choose(paxos.current_version() + 1, quorum);
                  }
                  propagate(paxos, address, quorum);
                }
                catch (elle::Error const& e)
                {
                  ELLE_WARN("rebalancing of %f failed: %s", elt.first, e);
                }
            }
          }
        }

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::LocalPeer::propose(PaxosServer::Quorum peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p)
        {
          ELLE_TRACE_SCOPE("%s: get proposal at %f: %s",
                           *this, address, p);
          auto& decision = this->_load(address, peers);
          auto res = decision.paxos.propose(std::move(peers), p);
          BlockOrPaxos data(&decision);
          this->storage()->set(
            address,
            elle::serialization::binary::serialize(data),
            true, true);
          return res;
        }

        Paxos::PaxosClient::Proposal
        Paxos::LocalPeer::accept(PaxosServer::Quorum peers,
                                 Address address,
                                 Paxos::PaxosClient::Proposal const& p,
                                 Value const& value)
        {
          ELLE_TRACE_SCOPE("%s: accept at %f: %s",
                           *this, address, p);
          // FIXME: factor with validate in doughnut::Local::store
          std::shared_ptr<blocks::Block> block;
          if (value.is<std::shared_ptr<blocks::Block>>())
            block = value.get<std::shared_ptr<blocks::Block>>();
          if (block)
          {
            ELLE_DEBUG("validate block")
              if (auto res = block->validate(this->doughnut())); else
                throw ValidationFailed(res.reason());
          }
          auto& decision = this->_load(address);
          auto& paxos = decision.paxos;
          if (block)
            if (auto previous = paxos.current_value())
            {
              auto valres = previous->value.
                template get<std::shared_ptr<blocks::Block>>()->
                validate(this->doughnut(), *block);
              if (!valres)
                throw Conflict("peer validation failed", block->clone());
            }
          auto res = paxos.accept(std::move(peers), p, value);
          {
            ELLE_DEBUG_SCOPE("store accepted paxos");
            BlockOrPaxos data(&decision);
            this->storage()->set(
              address,
              elle::serialization::binary::serialize(
                data, this->doughnut().version()),
              true, true);
          }
          if (block)
            this->on_store()(*block);
          return std::move(res);
        }

        void
        Paxos::LocalPeer::confirm(PaxosServer::Quorum peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p)
        {
          BENCH("confirm.local");
          ELLE_TRACE_SCOPE("%s: confirm %f at proposal %s",
                           *this, address, p);
          auto& decision = this->_load(address);
          bool had_value = bool(decision.paxos.current_value());
          decision.paxos.confirm(peers, p);
          ELLE_DEBUG("store confirmed paxos")
          {
            BlockOrPaxos data(&decision);
            auto ser = [&]
            {
              BENCH("confirm.storage");
              auto res = elle::serialization::binary::serialize(
                data, this->doughnut().version());
              return res;
            }();
            this->storage()->set(address, ser, true, true);
          }
          auto const& quorum = decision.paxos.current_quorum();
          if (!contains(quorum, this->doughnut().id()))
            ELLE_TRACE("%s: evicted from %f quorum", this, address)
              this->_remove(address);
          else
          {
            this->_cache(address, quorum);
            if (this->_rebalance_auto_expand &&
                !had_value &&
                decision.paxos.current_value() &&
                signed(decision.paxos.current_quorum().size()) < this->_factor)
              this->_rebalancable.put(std::make_pair(address, false));
          }
        }

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::LocalPeer::get(PaxosServer::Quorum peers, Address address,
                              boost::optional<int> local_version)
        {
          ELLE_TRACE_SCOPE("%s: get %f from %f", *this, address, peers);
          auto res = this->_load(address).paxos.get(peers);
          // Honor local_version
          if (local_version && res &&
              res->value.template is<std::shared_ptr<blocks::Block>>())
          {
            auto& block =
              res->value.template get<std::shared_ptr<blocks::Block>>();
            auto mb = std::dynamic_pointer_cast<blocks::MutableBlock>(block);
            ELLE_ASSERT(mb);
            if (mb->version() == *local_version)
              block.reset();
          }
          ELLE_DEBUG("%s: returning %s", *this, res);
          return res;
        }

        void
        Paxos::LocalPeer::_register_rpcs(RPCServer& rpcs)
        {
          Local::_register_rpcs(rpcs);
          namespace ph = std::placeholders;
          rpcs.add(
            "propose",
            std::function<
            boost::optional<Paxos::PaxosClient::Accepted>(
              PaxosServer::Quorum, Address,
              Paxos::PaxosClient::Proposal const&)> (
                [this, &rpcs](PaxosServer::Quorum q, Address a,
                              Paxos::PaxosClient::Proposal const& p)
                {
                  this->_require_auth(rpcs, true);
                  return this->propose(std::move(q), a, p);
                }));
          if (this->doughnut().version() < elle::Version(0, 5, 0))
            rpcs.add(
              "accept",
              std::function<
              Paxos::PaxosClient::Proposal(
                PaxosServer::Quorum, Address,
                Paxos::PaxosClient::Proposal const& p,
		std::shared_ptr<blocks::Block> const& b)>
              ([this, &rpcs] (PaxosServer::Quorum q, Address a,
                              Paxos::PaxosClient::Proposal const& p,
                              std::shared_ptr<blocks::Block> const& b)
               -> Paxos::PaxosClient::Proposal
              {
                this->_require_auth(rpcs, true);
                return this->accept(q, a, p, std::move(b));
              }));
          else
            rpcs.add(
              "accept",
              std::function<
              Paxos::PaxosClient::Proposal(
                PaxosServer::Quorum,
                Address,
                Paxos::PaxosClient::Proposal const& p,
                Value const& value)>
              ([this, &rpcs](PaxosServer::Quorum q,
                             Address a,
                             Paxos::PaxosClient::Proposal const& p,
                             Value const& value)
               {
                 this->_require_auth(rpcs, true);
                 return this->accept(std::move(q), a, p, value);
               }));
          rpcs.add(
            "confirm",
            std::function<
            void(
              PaxosServer::Quorum, Address,
              Paxos::PaxosClient::Proposal const&)>
            (std::bind(&LocalPeer::confirm, this, ph::_1, ph::_2, ph::_3)));
          rpcs.add(
            "get",
            std::function<
            boost::optional<Paxos::PaxosClient::Accepted>(
              PaxosServer::Quorum, Address, boost::optional<int>)>
            (std::bind(&LocalPeer::get, this, ph::_1, ph::_2, ph::_3)));
        }

        template <typename T>
        T&
        unconst(T const& v)
        {
          return const_cast<T&>(v);
        }

        std::unique_ptr<blocks::Block>
        Paxos::LocalPeer::_fetch(Address address,
                                 boost::optional<int> local_version) const
        {
          if (this->doughnut().version() >= elle::Version(0, 5, 0))
          {
            elle::serialization::Context context;
            context.set<Doughnut*>(&this->doughnut());
            auto data =
              elle::serialization::binary::deserialize<BlockOrPaxos>(
                this->storage()->get(address), true, context);
            // FIXME: this will trigger the retry with an immutable block type
            // in Consensus::fetch
            if (!data.block)
            {
              ELLE_TRACE("%s: fetch: no data block", *this);
              throw MissingBlock(address);
            }
            return std::unique_ptr<blocks::Block>(data.block.release());
          }
          // Backward compatibility pre-0.5.0
          auto decision = this->_addresses.find(address);
          if (decision == this->_addresses.end())
            try
            {
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto data =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  this->storage()->get(address), true, context);
              if (data.block)
              {
                ELLE_DEBUG("loaded immutable block from storage");
                return std::unique_ptr<blocks::Block>(data.block.release());
              }
              else
              {
                ELLE_DEBUG("loaded mutable block from storage");
                decision = const_cast<LocalPeer*>(this)->_addresses.emplace(
                  address, std::move(*data.paxos)).first;
              }
            }
            catch (storage::MissingKey const& e)
            {
              ELLE_TRACE("missing block %x", address);
              throw MissingBlock(e.key());
            }
          else
            ELLE_DEBUG("mutable block already loaded");
          auto& paxos = decision->second.paxos;
          if (auto highest = paxos.current_value())
          {
            auto version = highest->proposal.version;
            if (decision->second.chosen == version
              && highest->value.is<std::shared_ptr<blocks::Block>>())
            {
              ELLE_DEBUG("return already chosen mutable block");
              return highest->value.get<std::shared_ptr<blocks::Block>>()
                ->clone();
            }
            else
            {
              ELLE_TRACE_SCOPE(
                "finalize running Paxos for version %s (last chosen %s)"
                , version, decision->second.chosen);
              auto block = highest->value;
              Paxos::PaxosClient::Peers peers =
                lookup_nodes(
                  this->doughnut(), paxos.quorum_initial(), address);
              if (peers.empty())
                throw elle::Error(
                  elle::sprintf("No peer available for fetch %x", address));
              Paxos::PaxosClient client(uid(this->doughnut().keys().K()),
                                        std::move(peers));
              auto chosen = client.choose(version, block);
              // FIXME: factor with the end of doughnut::Local::store
              ELLE_DEBUG("%s: store chosen block", *this)
              unconst(decision->second).chosen = version;
              {
                BlockOrPaxos data(const_cast<Decision*>(&decision->second));
                this->storage()->set(
                  address,
                  elle::serialization::binary::serialize(
                    data,
                    this->doughnut().version()),
                  true, true);
              }
              // ELLE_ASSERT(block.unique());
              // FIXME: Don't clone, it's useless, find a way to steal
              // ownership from the shared_ptr.
              return block.get<std::shared_ptr<blocks::Block>>()->clone();
            }
          }
          else
          {
            ELLE_TRACE("%s: block has running Paxos but no value: %x",
                       *this, address);
            throw MissingBlock(address);
          }
        }

        void
        Paxos::LocalPeer::store(blocks::Block const& block, StoreMode mode)
        {
          ELLE_TRACE_SCOPE("%s: store %f", *this, block);
          ELLE_DEBUG("%s: validate block", *this)
            if (auto res = block.validate(this->doughnut())); else
              throw ValidationFailed(res.reason());
          if (!dynamic_cast<blocks::ImmutableBlock const*>(&block))
            throw ValidationFailed("bypassing Paxos for a non-immutable block");
          // validate with previous version
          try
          {
            auto previous_buffer = this->storage()->get(block.address());
            elle::IOStream s(previous_buffer.istreambuf());
            typename elle::serialization::binary::SerializerIn input(s);
            input.set_context<Doughnut*>(&this->doughnut());
            auto stored = input.deserialize<BlockOrPaxos>();
            if (!stored.block)
              ELLE_WARN("No block, cannot validate update");
            else
            {
              auto vr = stored.block->validate(this->doughnut(), block);
              if (!vr)
                if (vr.conflict())
                  throw Conflict(vr.reason(), stored.block->clone());
                else
                  throw ValidationFailed(vr.reason());
            }
          }
          catch (storage::MissingKey const&)
          {}
          elle::Buffer data =
            [&]
            {
              BlockOrPaxos b(const_cast<blocks::Block&>(block));
              auto res = elle::serialization::binary::serialize(
                b, this->doughnut().version());
              b.block.release();
              return res;
            }();
          this->storage()->set(block.address(), data,
                              mode == STORE_INSERT,
                              mode == STORE_UPDATE);
          this->on_store()(block);
        }

        void
        Paxos::LocalPeer::remove(Address address, blocks::RemoveSignature rs)
        {
          if (this->doughnut().version() >= elle::Version(0, 4, 0))
          {
            auto it = this->_addresses.find(address);
            ELLE_TRACE("paxos::remove, known=%s", it != this->_addresses.end());
            if (it != this->_addresses.end())
            {
              auto& decision = this->_addresses.at(address);
              auto& paxos = decision.paxos;
              if (auto highest = paxos.current_value())
              {
                auto& v = highest->value.get<std::shared_ptr<blocks::Block>>();
                auto valres = v->validate_remove(this->doughnut(), rs);
                ELLE_TRACE("mutable block remove validation gave %s", valres);
                if (!valres)
                  if (valres.conflict())
                    throw Conflict(valres.reason(), v->clone());
                  else
                    throw ValidationFailed(valres.reason());
              }
              else
                ELLE_WARN("No paxos accepted, cannot validate removal");
            }
            else
            { // immutable block
              elle::Buffer buffer;
              try
              {
                buffer = this->storage()->get(address);
              }
              catch (storage::MissingKey const& k)
              {
                throw MissingBlock(k.key());
              }
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto stored =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  buffer, true, context);
              if (!stored.block)
                ELLE_WARN("No paxos and no block, cannot validate removal");
              else
              {
                auto& previous = *stored.block;
                auto valres = previous.validate_remove(this->doughnut(), rs);
                ELLE_TRACE("Immutable block remove validation gave %s", valres);
                if (!valres)
                  if (valres.conflict())
                    throw Conflict(valres.reason(), previous.clone());
                  else
                    throw ValidationFailed(valres.reason());
              }
            }
          }
          this->_remove(address);
        }

        void
        Paxos::LocalPeer::_remove(Address address)
        {
          try
          {
            this->storage()->erase(address);
          }
          catch (storage::MissingKey const& k)
          {
            throw MissingBlock(k.key());
          }
          this->on_remove()(address);
          this->_addresses.erase(address);
        }

        static
        std::shared_ptr<blocks::Block>
        resolve(blocks::Block& b,
                blocks::Block& newest,
                StoreMode mode,
                ConflictResolver* resolver)
        {
          if (newest == b)
          {
            ELLE_DEBUG("Paxos chose another block version, which "
                       "happens to be the same as ours");
            return nullptr;
          }
          ELLE_DEBUG_SCOPE("Paxos chose another block value");
          if (resolver)
          {
            ELLE_TRACE_SCOPE("chosen block differs, run conflict resolution");
            auto resolved = (*resolver)(b, newest, mode);
            if (resolved)
              return std::shared_ptr<blocks::Block>(resolved.release());
            else
            {
              ELLE_TRACE("resolution failed");
              // FIXME: useless clone, find a way to steal ownership
              throw infinit::model::doughnut::Conflict(
                "Paxos chose a different value", newest.clone());
            }
          }
          else
          {
            ELLE_TRACE("chosen block differs, signal conflict");
            // FIXME: useless clone, find a way to steal ownership
            throw infinit::model::doughnut::Conflict(
              "Paxos chose a different value",
              newest.clone());
          }
        }

        void
        Paxos::_store(std::unique_ptr<blocks::Block> inblock,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          ELLE_TRACE_SCOPE("%s: store %f", *this, *inblock);
          std::shared_ptr<blocks::Block> b(inblock.release());
          ELLE_ASSERT(b);
          overlay::Operation op;
          switch (mode)
          {
            case STORE_INSERT:
              op = overlay::OP_INSERT;
              break;
            case STORE_UPDATE:
              op = overlay::OP_UPDATE;
              break;
            default:
              elle::unreachable();
          }
          auto owners = this->_owners(b->address(), this->_factor, op);
          if (dynamic_cast<blocks::MutableBlock*>(b.get()))
          {
            Paxos::PaxosClient::Peers peers;
            PaxosServer::Quorum peers_id;
            // FIXME: This void the "query on the fly" optimization as it forces
            // resolution of all peers to get their id. Any other way ?
            for (auto wpeer: owners)
            {
              auto peer = wpeer.lock();
              if (!peer)
                ELLE_WARN("%s: peer was deleted while storing", this);
              else
              {
                peers_id.insert(peer->id());
                peers.push_back(
                  elle::make_unique<Peer>(wpeer, b->address()));
              }
            }
            if (peers.empty())
              throw elle::Error(
                elle::sprintf("No peer available for store %s %x",
                  op == overlay::OP_INSERT ? "insert" : "update",
                  b->address()));
            ELLE_DEBUG("owners: %f", peers);
            // FIXME: client is persisted on conflict resolution, hence the
            // round number is kept and won't start at 0.
            // Keep retrying with new quorums
            while (true)
            {
              try
              {
                Paxos::PaxosClient client(
                  uid(this->doughnut().keys().K()), std::move(peers));
                // Keep resolving conflicts and retrying
                while (true)
                {
                  auto mb = dynamic_cast<blocks::MutableBlock*>(b.get());
                  auto version = mb->version();
                  boost::optional<Paxos::PaxosServer::Accepted> chosen;
                  ELLE_DEBUG("run Paxos for version %s", version)
                    chosen = client.choose(version, b);
                  if (chosen)
                  {
                    if (chosen->value.is<PaxosServer::Quorum>())
                    {
                      auto const& q = chosen->value.get<PaxosServer::Quorum>();
                      ELLE_DEBUG_SCOPE("Paxos elected another quorum: %f", q);
                      b->seal(chosen->proposal.version + 1);
                      throw Paxos::PaxosServer::WrongQuorum(q, peers_id);
                    }
                    else
                    {
                      auto block =
                        chosen->value.get<std::shared_ptr<blocks::Block>>();
                      if (!(b = resolve(*b, *block, mode, resolver.get())))
                        break;
                      ELLE_DEBUG("seal resolved block")
                        b->seal(chosen->proposal.version + 1);
                    }
                  }
                  else
                    break;
                }
              }
              catch (Paxos::PaxosServer::WrongQuorum const& e)
              {
                ELLE_TRACE("%s", e.what());
                peers = lookup_nodes(
                  this->doughnut(), e.expected(), b->address());
                peers_id.clear();
                for (auto const& peer: peers)
                  peers_id.insert(static_cast<Peer&>(*peer).id());
                continue;
              }
              break;
            }
          }
          else
          {
            elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
            {
              for (auto wpeer: owners)
              {
                auto peer = wpeer.lock();
                if (!peer)
                  ELLE_WARN("peer was deleted while storing");
                else
                  peer->store(*b, mode);
              }
            };
          }
        }

        class Hit
        {
        public:
          Hit(Address node)
            : _node(node)
            , _quorum()
            , _accepted()
          {}

          Hit(Address node,
              std::pair<Paxos::PaxosServer::Quorum,
                        std::unique_ptr<Paxos::PaxosClient::Accepted>> data)
            : _node(node)
            , _quorum(std::move(data.first))
            , _accepted(data.second ?
                        std::move(*data.second) :
                        boost::optional<Paxos::PaxosClient::Accepted>())
          {}

          Hit()
          {}

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("quorum", this->_quorum);
            s.serialize("accepted", this->_accepted);
          }

          ELLE_ATTRIBUTE_R(model::Address, node);
          ELLE_ATTRIBUTE_R(boost::optional<Paxos::PaxosServer::Quorum>,
                           quorum);
          ELLE_ATTRIBUTE_R(boost::optional<Paxos::PaxosClient::Accepted>,
                           accepted);
        };

        std::unique_ptr<blocks::Block>
        Paxos::_fetch(Address address, boost::optional<int> local_version)
        {
          if (this->doughnut().version() < elle::Version(0, 5, 0))
          {
            auto peers =
              this->_owners(address, this->_factor, overlay::OP_FETCH);
            return fetch_from_members(peers, address, std::move(local_version));
          }
          auto peers = this->_peers(address, local_version);
          if (peers.empty())
          {
            ELLE_TRACE("could not find any owner for %s", address);
            throw MissingBlock(address);
          }
          else
            ELLE_DEBUG("owners: %s", peers);
          while (true)
            try
            {
              if (address.mutable_block())
              {
                ELLE_DEBUG_SCOPE("run paxos");
                Paxos::PaxosClient client(
                  uid(this->doughnut().keys().K()), std::move(peers));
                if (auto res = client.get())
                  if (*res)
                  {
                    // FIXME: steal ownership
                    ELLE_DEBUG("received new block");
                    return res.get()->clone();
                  }
                  else
                  {
                    ELLE_DEBUG("local version is the most recent");
                    return {}; // local_version matched
                  }
                else
                {
                  ELLE_DEBUG("no value can be retreived");
                  throw MissingBlock(address);
                }
              }
              else
              {
                std::unique_ptr<blocks::Block> res;
                for (auto const& peer: peers)
                {
                  try
                  {
                    if (auto member = static_cast<Peer&>(*peer).member().lock())
                      return member->fetch(address, local_version);
                    else
                      ELLE_WARN("%s: peer was deleted while storing", this);
                  }
                  catch (elle::Error const& e)
                  {
                    ELLE_TRACE("error fetching from %s: %s", *peer, e.what());
                  }
                }
                throw  MissingBlock(address);
              }
            }
            catch (Paxos::PaxosServer::WrongQuorum const& e)
            {
              // FIXME: in some situation, even with a wrong quorum, we can have
              // a valid reduce (e.g. if we miss one host but have a majority)
              ELLE_DEBUG("%s", e.what());
              peers = lookup_nodes(this->doughnut(), e.expected(), address);
            }
        }

        Paxos::PaxosClient::Peers
        Paxos::_peers(Address const& address,
                      boost::optional<int> local_version)
        {
          auto owners =
            this->_owners(address, this->_factor, overlay::OP_FETCH);
          PaxosClient::Peers peers;
          for (auto peer: owners)
            peers.push_back(
              elle::make_unique<Peer>(peer, address, local_version));
          ELLE_DEBUG("peers: %f", peers);
          return peers;
        }

        Paxos::PaxosClient
        Paxos::_client(Address const& address)
        {
          return Paxos::PaxosClient(
            uid(this->doughnut().keys().K()), this->_peers(address));
        }

        std::pair<Paxos::PaxosServer::Quorum, int>
        Paxos::_latest(PaxosClient& client, Address address)
        {
          int version = 0;
          while (true)
            try
            {
              auto last = client.get_quorum();
              // FIXME: Couldn't we operate on MutableBlocks directly in Paxos ?
              if (last.first)
                version = std::dynamic_pointer_cast<blocks::MutableBlock>(
                  *last.first)->version();
              return std::make_pair<>(last.second, version);
            }
            catch (Paxos::PaxosServer::WrongQuorum const& e)
            {
              client.peers(
                lookup_nodes(this->doughnut(), e.expected(), address));
            }
        }

        bool
        Paxos::rebalance(Address address)
        {
          ELLE_TRACE_SCOPE("%s: rebalance %f", *this, address);
          auto client = this->_client(address);
          return this->_rebalance(client, address);
        }

        Paxos::PaxosServer::Quorum
        Paxos::_rebalance_extend_quorum(Address address,
                                        PaxosServer::Quorum q)
        {
          // Make sure we didn't lose a previous owner because of the overlay
          // failing to look it up.
          PaxosServer::Quorum new_q(q);
          for (auto const& wowner: this->_owners(
                 address, this->_factor, overlay::OP_INSERT))
          {
            if (signed(new_q.size()) >= this->_factor)
              break;
            if (auto owner = wowner.lock())
              new_q.emplace(owner->id());
          }
          return new_q;
        }

        bool
        Paxos::_rebalance(PaxosClient& client, Address address)
        {
          ELLE_ASSERT_GTE(this->doughnut().version(), elle::Version(0, 5, 0));
          auto latest = this->_latest(client, address);
          // FIXME: handle immutable block errors
          ELLE_DEBUG("quorum: %f", latest.first);
          if (signed(latest.first.size()) == this->_factor)
          {
            ELLE_TRACE("block is already well balanced (%s replicas)",
                       this->_factor);
            return false;
          }
          auto new_q = this->_rebalance_extend_quorum(address, latest.first);
          if (new_q == latest.first)
          {
            ELLE_TRACE("unable to find any new owner");
            return false;
          }
          ELLE_DEBUG("rebalance block to: %f", new_q)
            return this->_rebalance(client, address, new_q, latest.second);
        }

        bool
        Paxos::rebalance(Address address, PaxosClient::Quorum const& ids)
        {
          ELLE_TRACE_SCOPE("%s: rebalance %f to %f", *this, address, ids);
          auto client = this->_client(address);
          auto latest = this->_latest(client, address);
          return this->_rebalance(client, address, ids, latest.second);
        }

        bool
        Paxos::_rebalance(PaxosClient& client,
                          Address address,
                          PaxosClient::Quorum const& ids,
                          int version)
        {
          std::unique_ptr<PaxosClient> replace;
          while (true)
          {
            try
            {
              // FIXME: version is the last *value* version, there could have
              // been a quorum since then in which case this will fail.
              if (auto conflict =
                  (replace ? *replace : client).choose(version + 1, ids))
              {
                // FIXME: Retry balancing.
                // FIXME: We should still try block propagation in the case that
                // "someone else" failed to perform it.
                if (conflict->value.is<PaxosServer::Quorum>())
                {
                  auto quorum = conflict->value.get<PaxosServer::Quorum>();
                  if (quorum == ids)
                    ELLE_TRACE("someone else rebalanced to the same quorum");
                  else if (signed(quorum.size()) == this->_factor)
                    ELLE_TRACE(
                      "someone else rebalanced to a sufficient quorum");
                  else
                  {
                    ELLE_TRACE(
                      "someone else rebalanced to an insufficient quorum");
                    auto new_q =
                      this->_rebalance_extend_quorum(address, quorum);
                    if (new_q == quorum)
                    {
                      ELLE_TRACE("unable to find any new owner");
                      return false;
                    }
                    ++version;
                    replace.reset(
                      new Paxos::PaxosClient(
                        this->doughnut().id(),
                        lookup_nodes(this->doughnut(), new_q, address)));
                    continue;
                  }
                }
                else
                  ELLE_WARN(
                    "someone else picked a value while we rebalanced");
                return false;
              }
              else
              {
                ELLE_TRACE("successfully rebalanced to %s nodes at version %s",
                           ids.size(), version + 1);
                return true;
              }
            }
            catch (Paxos::PaxosServer::WrongQuorum const& e)
            {
              replace.reset(
                new Paxos::PaxosClient(
                  this->doughnut().id(),
                  lookup_nodes(this->doughnut(), e.expected(), address)));
            }
            catch (elle::Error const&)
            {
              ELLE_WARN("rebalancing failed: %s", elle::exception_string());
              return false;
            }
          }
        }

        void
        Paxos::_remove(Address address, blocks::RemoveSignature rs)
        {
          this->remove_many(address, std::move(rs), this->_factor);
        }

        Paxos::LocalPeer::Decision::Decision(PaxosServer paxos)
          : chosen(-1)
          , paxos(std::move(paxos))
        {}

        Paxos::LocalPeer::Decision::Decision(
          elle::serialization::SerializerIn& s)
          : chosen(s.deserialize<int>("chosen"))
          , paxos(s.deserialize<PaxosServer>("paxos"))
        {}

        void
        Paxos::LocalPeer::Decision::serialize(
          elle::serialization::Serializer& s)
        {
          s.serialize("chosen", this->chosen);
          s.serialize("paxos", this->paxos);
        }

        Paxos::LocalPeer::BlockRepartition::BlockRepartition(
          Address address_, PaxosServer::Quorum quorum_)
          : address(address_)
          , quorum(std::move(quorum_))
        {}

        int
        Paxos::LocalPeer::BlockRepartition::replication_factor() const
        {
          return this->quorum.size();
        }

        /*-----.
        | Stat |
        `-----*/

        typedef std::unordered_map<std::string, boost::optional<Hit>> Hits;

        class PaxosStat
          : public Consensus::Stat
        {
        public:
          PaxosStat(Hits hits)
            : _hits(std::move(hits))
          {}

          virtual
          void
          serialize(elle::serialization::Serializer& s) override
          {
            s.serialize("hits", this->_hits);
          }

          ELLE_ATTRIBUTE_R(Hits, hits);
        };

        std::unique_ptr<Consensus::Stat>
        Paxos::stat(Address const& address)
        {
          ELLE_TRACE_SCOPE("%s: stat %s", *this, address);
          // // ELLE_ASSERT_GTE(this->doughnut().version(), elle::Version(0, 5, 0));
          // auto hits = _Details::_multifetch_paxos(*this, address);
          // auto peers = this->_owners(address, this->_factor, overlay::OP_FETCH);
          // Hits stat_hits;
          // for (auto& hit: hits)
          // {
          //   auto node = elle::sprintf("%s", hit.node());
          //   stat_hits.emplace(node, std::move(hit));
          // }
          // return elle::make_unique<PaxosStat>(std::move(stat_hits));
          return Super::stat(address);
        }

        /*--------------.
        | Configuration |
        `--------------*/

        Paxos::Configuration::Configuration(int replication_factor)
          : consensus::Configuration()
          , _replication_factor(replication_factor)
        {}

        std::unique_ptr<Consensus>
        Paxos::Configuration::make(model::doughnut::Doughnut& dht)
        {
          return elle::make_unique<Paxos>(dht, this->_replication_factor);
        }

        Paxos::Configuration::Configuration(
          elle::serialization::SerializerIn& s)
        {
          this->serialize(s);
        }

        void
        Paxos::Configuration::serialize(elle::serialization::Serializer& s)
        {
          consensus::Configuration::serialize(s);
          s.serialize("replication-factor", this->_replication_factor);
        }

        static const elle::serialization::Hierarchy<Configuration>::
        Register<Paxos::Configuration> _register_Configuration("paxos");
      }

      static const elle::TypeInfo::RegisterAbbrevation
      _dht_abbr("consensus::Paxos::LocalPeer", "PaxosLocal");
    }
  }
}

namespace athena
{
  namespace paxos
  {
    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<TooFewPeers> _register_serialization;
  }
}
