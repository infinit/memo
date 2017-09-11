#include <memo/model/doughnut/consensus/Paxos.hh>

#include <functional>
#include <utility>

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <elle/algorithm.hh>
#include <elle/bench.hh>
#include <elle/find.hh>
#include <elle/memory.hh>
#include <elle/multi_index_container.hh>
#include <elle/random.hh>
#include <elle/range.hh>
#include <elle/serialization/json/Error.hh>

#include <elle/cryptography/rsa/PublicKey.hh>
#include <elle/cryptography/hash.hh>

#include <elle/das/serializer.hh>

#include <elle/reactor/Backoff.hh>
#include <elle/reactor/for-each.hh>

#include <memo/RPC.hh>

#include <memo/model/Conflict.hh>
#include <memo/model/MissingBlock.hh>
#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/Local.hh>
#include <memo/model/doughnut/Remote.hh>
#include <memo/model/doughnut/DummyPeer.hh>
#include <memo/model/doughnut/ACB.hh>
#include <memo/model/blocks/ImmutableBlock.hh>
#include <memo/model/doughnut/OKB.hh>
#include <memo/model/doughnut/ValidationFailed.hh>
#include <memo/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Paxos");

#define BENCH(name)                                      \
  static auto bench = elle::Bench<>{"bench.paxos." name, 10000s}; \
  auto bs = bench.scoped()

ELLE_DAS_SERIALIZE(
  memo::model::doughnut::consensus::Paxos::PaxosServer::Response);

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        using boost::adaptors::filtered;
        using boost::adaptors::transformed;
        using boost::algorithm::any_of;

        /// Run `f`, translating possible network errors into Paxos
        /// exceptions.
        template<typename F>
        auto translate_exceptions(std::string const& name, F f)
          -> decltype(f())
        {
          try
          {
            return f();
          }
          catch (elle::reactor::network::Error const& e)
          {
            ELLE_TRACE("network exception in paxos: %s", e);
            throw elle::athena::paxos::Unavailable();
          }
          catch (MissingBlock const& e)
          {
            ELLE_TRACE("weak error in paxos: %s", e);
            throw elle::athena::paxos::WeakError(std::current_exception());
          }
        }

        BlockOrPaxos::BlockOrPaxos(blocks::Block& b)
          : block(&b, [] (blocks::Block*) {})
          , paxos()
        {}

        /*----------------------------.
        | LocalPeer::BlockRepartition |
        `----------------------------*/


        Paxos::LocalPeer::BlockRepartition::BlockRepartition(
          Address a, bool immutable, PaxosServer::Quorum q)
          : address(a)
          , immutable(immutable)
          , quorum(std::move(q))
        {}

        bool
        Paxos::LocalPeer::BlockRepartition::operator ==(
          BlockRepartition const& rhs) const
        {
          return std::tie(this->address, this->immutable, this->quorum)
            == std::tie(rhs.address, rhs.immutable, rhs.quorum);
        }

        int
        Paxos::LocalPeer::BlockRepartition::replication_factor() const
        {
          return this->quorum.size();
        }

        struct Paxos::LocalPeer::BlockRepartition::HashByAddress
        {
          std::size_t
          operator()(Paxos::LocalPeer::BlockRepartition const& r) const
          {
            return std::hash<Address>()(r.address);
          }
        };

        /*-------------.
        | BlockOrPaxos |
        `-------------*/

        BlockOrPaxos::BlockOrPaxos(Paxos::LocalPeer::Decision* p)
          : paxos(p, [] (Paxos::LocalPeer::Decision*) {})
        {}

        BlockOrPaxos::BlockOrPaxos(elle::serialization::SerializerIn& s)
          : block(nullptr, std::default_delete<blocks::Block>())
          , paxos(nullptr, std::default_delete<Paxos::LocalPeer::Decision>())
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
                     bool rebalance_inspect,
                     Duration node_timeout)
          : Super(doughnut)
          , _factor(factor)
          , _lenient_fetch(memo::getenv("PAXOS_LENIENT_FETCH", lenient_fetch))
          , _rebalance_auto_expand(rebalance_auto_expand)
          , _rebalance_inspect(rebalance_inspect)
          , _node_timeout(node_timeout)
        {}

        /*--------.
        | Factory |
        `--------*/

        std::unique_ptr<Local>
        Paxos::make_local(boost::optional<int> port,
                          boost::optional<boost::asio::ip::address> listen_address,
                          std::unique_ptr<silo::Silo> storage)
        {
          return std::make_unique<consensus::Paxos::LocalPeer>(
            *this,
            this->factor(),
            this->_rebalance_auto_expand,
            this->_rebalance_inspect,
            this->_node_timeout,
            this->doughnut(),
            this->doughnut().id(),
            std::move(storage),
            port.value_or(0),
            listen_address);
        }

        std::shared_ptr<Remote>
        Paxos::make_remote(std::shared_ptr<Dock::Connection> connection)
        {
          return std::make_shared<Paxos::RemotePeer>(this->doughnut(),
                                                     std::move(connection));
        }

        /*-----.
        | Peer |
        `-----*/

        std::shared_ptr<Paxos::Peer>
        to_paxos_peer(overlay::Overlay::WeakMember wpeer)
        {
          if (auto peer = wpeer.lock())
            return ELLE_ENFORCE(std::dynamic_pointer_cast<Paxos::Peer>(peer));
          else
            return nullptr;
        }

        class PaxosPeer
          : public Paxos::PaxosClient::Peer
        {
        public:
          PaxosPeer(overlay::Overlay::WeakMember member,
                    Address address,
                    boost::optional<int> local_version,
                    bool insert)
            : Paxos::PaxosClient::Peer((ELLE_ASSERT(member.lock()),
                                        member.lock()->id()))
            , _member(std::dynamic_pointer_cast<Paxos::Peer>(std::move(member)))
            , _address(address)
            , _local_version(local_version)
            , _insert(insert)
          {
            if (!this->_member.lock())
              ELLE_ABORT("invalid paxos peer: %s", member);
          }

          std::shared_ptr<Paxos::Peer>
          _lock_member()
          {
            if (auto member = this->_member.lock())
              return member;
            else
            {
              ELLE_WARN("%s: peer %f was deleted", this, this->id());
              throw elle::athena::paxos::Unavailable();
            }
          }

          Paxos::PaxosServer::Response
          propose(Paxos::PaxosClient::Quorum const& q,
                  Paxos::PaxosClient::Proposal const& p) override
          {
            BENCH("propose");
            auto member = this->_lock_member();
            return translate_exceptions("propose",
              [&]
              {
                return member->propose(q, this->_address, p, this->_insert);
              });
          }

          Paxos::PaxosClient::Proposal
          accept(Paxos::PaxosClient::Quorum const& q,
                 Paxos::PaxosClient::Proposal const& p,
                 Paxos::Value const& value) override
          {
            BENCH("accept");
            auto member = this->_lock_member();
            return translate_exceptions("accept",
              [&]
              {
                return member->accept(q, this->_address, p, value);
              });
          }

          void
          confirm(Paxos::PaxosClient::Quorum const& q,
                  Paxos::PaxosClient::Proposal const& p) override
          {
            BENCH("confirm");
            auto member = this->_lock_member();
            return translate_exceptions("confirm",
              [&]
              {
                if (member->doughnut().version() >= elle::Version(0, 5, 0))
                  member->confirm(q, this->_address, p);
              });
          }

          boost::optional<Paxos::PaxosClient::Accepted>
          get(Paxos::PaxosClient::Quorum const& q) override
          {
            BENCH("get");
            auto member = this->_lock_member();
            return translate_exceptions("get",
              [&]
              {
                this->_missing = false;
                try
                {
                  return member->get(q, this->_address, this->_local_version);
                }
                catch (MissingBlock const&)
                {
                  this->_missing = true;
                  throw;
                }
              });
          }

          ELLE_ATTRIBUTE_R(std::ambivalent_ptr<Paxos::Peer>, member);
          ELLE_ATTRIBUTE(Address, address);
          ELLE_ATTRIBUTE(boost::optional<int>, local_version);
          ELLE_ATTRIBUTE(bool, insert);
          ELLE_ATTRIBUTE_R(boost::optional<bool>, missing);
        };

        /*--------.
        | Details |
        `--------*/

        struct Paxos::Details
        {
          template <typename Peers>
          static
          int
          send_immutable_block(Paxos& self,
                               Peers&& peers,
                               blocks::Block const& b,
                               PaxosClient::Quorum current)
          {
            std::vector<std::shared_ptr<Paxos::Peer>> reached;
            std::vector<std::shared_ptr<Paxos::Peer>> to_confirm;
            ELLE_TRACE_SCOPE("send block to {}", peers);
            std::exception_ptr weak_error;
            elle::reactor::for_each_parallel(
              std::forward<Peers>(peers) | transformed(to_paxos_peer)
              // FIXME: boost filtered does not play well with input iterator
              // that move their value, since it dereferences it twice.  |
              // filtered([] (auto peer) { return bool(peer); })
              ,
              [&] (auto peer)
              {
                if (!peer)
                {
                  ELLE_WARN("peer was deleted while storing");
                  return;
                }
                if (elle::find(current, peer->id()))
                {
                  to_confirm.emplace_back(peer);
                  return;
                }
                try
                {
                  ELLE_DEBUG_SCOPE("send block to {}", peer);
                  peer->store(b, STORE_INSERT);
                  reached.emplace_back(peer);
                  to_confirm.emplace_back(peer);
                  current.insert(peer->id());
                }
                catch (elle::athena::paxos::Unavailable const& e)
                {
                  ELLE_TRACE("sending block to {} failed: {}", peer, e);
                }
                catch (elle::athena::paxos::WeakError const& e)
                {
                  ELLE_TRACE("weak error sending block to {}: {}", peer, e);
                  if (!weak_error)
                    weak_error = e.exception();
                }
              });
            if (reached.size() == 0u)
            {
              if (weak_error)
              {
                ELLE_TRACE("rethrow weak error: {}",
                           elle::exception_string(weak_error));
                std::rethrow_exception(weak_error);
              }
              return 0;
            }
            if (self.doughnut().version() >= elle::Version(0, 6, 0))
              ELLE_TRACE("confirm block to {}", to_confirm)
                elle::reactor::for_each_parallel(
                  to_confirm,
                  [&] (auto peer)
                  {
                    if (!peer)
                    {
                      ELLE_WARN("peer was deleted while confirming");
                      return;
                    }
                    try
                    {
                      peer->confirm(current, b.address(), PaxosClient::Proposal());
                    }
                    catch (elle::athena::paxos::Unavailable const& e)
                    {
                      ELLE_TRACE("confirming block to {} failed: {}", peer, e);
                    }
                  });
            return reached.size();
          }

          using Peers = std::vector<std::unique_ptr<PaxosPeer>>;

          static
          Peers
          _peers(Paxos& self,
                 Address const& address,
                 boost::optional<int> local_version = {})
          {
            auto owners = self.doughnut().overlay()->lookup(
              address, self._factor, address.mutable_block());
            Peers peers;
            for (auto peer: owners)
              // If the overlay yields, the member can be deleted in between.
              if (auto lock = peer.lock())
                peers.emplace_back(std::make_unique<PaxosPeer>(
                                     peer, address, local_version, false));
            ELLE_DEBUG("peers: %f", peers);
            return peers;
          }

          static
          std::unique_ptr<blocks::Block>
          _fetch(Paxos& self,
                 Address address,
                 Peers peers,
                 boost::optional<int> local_version)
          {
            if (peers.empty())
            {
              ELLE_TRACE("could not find any owner for %s", address);
              throw MissingBlock(address);
            }
            while (true)
              try
              {
                if (address.mutable_block())
                {
                  BENCH("_fetch.run");
                  ELLE_DEBUG_SCOPE("run paxos");
                  Paxos::PaxosClient client(
                    self.doughnut().id(), std::move(peers));
                  auto state = [&]
                    {
                      try
                      {
                        return client.state();
                      }
                      catch (MissingBlock const&)
                      {
                        auto const not_missing = [] (auto const& peer)
                        {
                          return
                            !static_cast<PaxosPeer&>(*peer).missing().get();
                        };
                        elle::reactor::for_each_parallel(
                          client.peers() | filtered(not_missing),
                          [&] (auto const& peer)
                          {
                            ELLE_TRACE("reconcile %f on %f", address, peer)
                              static_cast<PaxosPeer&>(*peer)._lock_member()
                              ->reconcile(address);
                          });
                        throw;
                      }
                    }();
                  if (state.value)
                    if (*state.value)
                    {
                      ELLE_ASSERT(state.proposal);
                      // FIXME: steal ownership instead of cloning
                      ELLE_DEBUG("received new block");
                      auto res = std::dynamic_pointer_cast<blocks::MutableBlock>(
                        (*state.value)->clone());
                      if (state.proposal->version != res->version())
                      {
                        auto v = state.proposal->version + 1;
                        ELLE_DEBUG("bump seal version from {} to {}",
                                   res->version(), v);
                        res->seal_version(v);
                      }
                      return std::move(res);
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
                  static bool const balance =
                    !elle::os::getenv("INFINIT_DISABLE_BALANCED_TRANSFERS", false);
                  if (balance && peers.size() > 1)
                  {
                    elle::shuffle(peers);
                    boost::sort(
                      peers,
                      [&self] (auto const& p1, auto const& p2)
                      {
                        return
                          self._transfers[p1->id()] < self._transfers[p2->id()];
                      });
                  }
                  ELLE_DUMP("%s: will try peers in that order: %s", self, peers);
                  for (auto const& peer: peers)
                  {
                    try
                    {
                      self._transfers[peer->id()]++;
                      elle::SafeFinally at_end([&] { self._transfers[peer->id()]--;});
                      if (auto member = static_cast<PaxosPeer&>(*peer).member().lock())
                        return member->fetch(address, local_version);
                      else
                        ELLE_WARN("%s: peer was deleted while storing", self);
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
                peers = Details::lookup_nodes(
                  self.doughnut(), e.expected(), address, local_version);
              }
          }


          template <typename Quorum>
          static
          Details::Peers
          lookup_nodes(Doughnut& dht,
                       Quorum&& quorum,
                       Address address,
                       boost::optional<int> local_version = boost::none,
                       bool insert = false)
          {
            std::vector<std::unique_ptr<PaxosPeer>> res;
            for (auto member: dht.overlay()->lookup_nodes(
                   Paxos::PaxosServer::Quorum{begin(quorum), end(quorum)}))
              // If the overlay yields, the member can be deleted in between.
              if (auto lock = member.lock())
              {
                res.emplace_back(std::make_unique<PaxosPeer>(
                                   std::move(member),
                                   address,
                                   local_version,
                                   insert));
              }
            return res;
          }

          static
          void
          _propagate(Paxos::LocalPeer& self,
                     Address a,
                     PaxosClient::Proposal const& proposal,
                     PaxosClient::Quorum old_quorum,
                     PaxosClient::Quorum new_quorum)
          {
            if (auto it = elle::find(self._addresses, a))
            {
              auto decision = it->decision;
              auto& paxos = decision->paxos;
              if (auto value = paxos.current_value())
              {
                elle::reactor::for_each_parallel(
                  Details::lookup_nodes(
                    self.doughnut(),
                    new_quorum |
                    filtered([&] (auto id)
                             { return !elle::find(old_quorum, id); }),
                    a),
                  [&] (auto const& peer)
                  {
                    ELLE_DEBUG_SCOPE("propagate block value to %f at %f",
                                     peer, proposal);
                    ELLE_ENFORCE(paxos.state());
                    if (auto p = peer->member().lock())
                      p->propagate(
                        new_quorum,
                        value->value.get<std::shared_ptr<model::blocks::Block>>(),
                        proposal);
                  });
              }
            }
          }
        };

        /*-----.
        | Peer |
        `-----*/

        Paxos::Peer::Peer(Doughnut& dht, model::Address id)
          : Super(dht, id)
        {}

        /*-----------.
        | RemotePeer |
        `-----------*/

        Paxos::PaxosServer::Response
        Paxos::RemotePeer::propose(PaxosServer::Quorum const& peers,
                                   Address address,
                                   PaxosClient::Proposal const& p,
                                   bool insert)
        {
          if (this->doughnut().version() >= elle::Version(0, 9, 0))
          {
            using Propose =
              auto (PaxosServer::Quorum,
                    Address,
                    PaxosClient::Proposal const&,
                    bool)
              -> Paxos::PaxosServer::Response;
            auto propose = this->make_rpc<Propose>("propose");
            propose.set_context<Doughnut*>(&this->_doughnut);
            return propose(peers, address, p, insert);
          }
          else
          {
            using Propose =
              auto (PaxosServer::Quorum,
                    Address,
                    PaxosClient::Proposal const&)
              -> Paxos::PaxosServer::Response;
            auto propose = this->make_rpc<Propose>("propose");
            propose.set_context<Doughnut*>(&this->_doughnut);
            return propose(peers, address, p);
          }
        }

        Paxos::PaxosClient::Proposal
        Paxos::RemotePeer::accept(PaxosServer::Quorum const& peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p,
                                  Value const& value)
        {
          if (this->doughnut().version() < elle::Version(0, 5, 0))
          {
            if (!value.is<std::shared_ptr<blocks::Block>>())
            {
              ELLE_TRACE("unmanageable accept on non-block value");
              throw elle::reactor::network::Error("Peer unavailable");
            }
            using Accept =
              auto (PaxosServer::Quorum peers,
                    Address,
                    Paxos::PaxosClient::Proposal const&,
                    std::shared_ptr<blocks::Block>)
              -> Paxos::PaxosClient::Proposal;
            auto accept = this->make_rpc<Accept>("accept");
            accept.set_context<Doughnut*>(&this->_doughnut);
            return accept(peers, address, p,
                          value.get<std::shared_ptr<blocks::Block>>());
          }
          else
          {
            using Accept =
              auto (PaxosServer::Quorum peers,
                    Address,
                    Paxos::PaxosClient::Proposal const&,
                    Value const&)
              -> Paxos::PaxosClient::Proposal;
            auto accept = this->make_rpc<Accept>("accept");
            accept.set_context<Doughnut*>(&this->_doughnut);
            return accept(peers, address, p, value);
          }
        }

        void
        Paxos::RemotePeer::confirm(PaxosServer::Quorum const& peers,
                                   Address address,
                                   PaxosClient::Proposal const& p)
        {
          using Confirm =
            auto (PaxosServer::Quorum,
                  Address,
                  PaxosClient::Proposal const&)
            -> void;
          auto confirm = this->make_rpc<Confirm>("confirm");
          confirm.set_context<Doughnut*>(&this->_doughnut);
          return confirm(peers, address, p);
        }

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::RemotePeer::get(PaxosServer::Quorum const& peers,
                               Address address,
                               boost::optional<int> local_version)
        {
          using Get =
            auto (PaxosServer::Quorum, Address, boost::optional<int>)
            -> boost::optional<PaxosClient::Accepted>;
          auto get = this->make_rpc<Get>("get");
          get.set_context<Doughnut*>(&this->_doughnut);
          return get(peers, address, local_version);
        }

        bool
        Paxos::RemotePeer::reconcile(Address address)
        {
          return translate_exceptions("reconcile",
            [&]
            {
              using Reconcile = auto (Address) -> bool;
              auto reconcile = this->make_rpc<Reconcile>("reconcile");
              reconcile.set_context<Doughnut*>(&this->_doughnut);
              return reconcile(address);
            });
        }

        void
        Paxos::RemotePeer::propagate(PaxosServer::Quorum  q,
                                     std::shared_ptr<blocks::Block> block,
                                     Paxos::PaxosClient::Proposal p)
        {
          return translate_exceptions("propagate",
            [&]
            {
              using Propagate = void (PaxosServer::Quorum  q,
                                      std::shared_ptr<blocks::Block> block,
                                      Paxos::PaxosClient::Proposal p);
              auto propagate = this->make_rpc<Propagate>("propagate");
              propagate.set_context<Doughnut*>(&this->_doughnut);
              return propagate(std::move(q), std::move(block), std::move(p));
            });
        }

        void
        Paxos::RemotePeer::store(blocks::Block const& block, StoreMode mode)
        {
          translate_exceptions("store",
            [&]
            {
              Super::store(block, mode);
            });
        }


        /*----------.
        | LocalPeer |
        `----------*/

        Paxos::LocalPeer::~LocalPeer()
        try
        {
          ELLE_TRACE_SCOPE("%s: destruct", *this);
          // Make sure no eviction thread is started during cleanup.
          for (auto& timeout: this->_node_timeouts)
            timeout.second.cancel();
          // Avoid exceptions from unique_ptr and vector destructors.
          this->_rebalance_thread.terminate_now();
          for (auto& t: this->_evict_threads)
            if (t)
              t->terminate_now();
        }
        catch (...)
        {
          ELLE_ABORT("exception escaping %s destructor: %s",
                     elle::type_info<Paxos>(),
                     elle::exception_string());
        }

        void
        Paxos::LocalPeer::initialize()
        {
          this->doughnut().overlay()->on_discovery().connect(
            [this] (NodeLocation node, bool observer)
            {
              if (!observer)
                this->_discovered(node.id());
            });
          this->doughnut().overlay()->on_disappearance().connect(
            [this] (Address id, bool observer)
            {
              if (!observer)
                this->_disappeared(id);
            });
          if (this->_rebalance_inspect && this->_factor > 1)
            this->_rebalance_inspector.reset(
              new elle::reactor::Thread(
                elle::sprintf("%s: rebalancing inspector", this),
                [this]
                {
                  ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Paxos.rebalance");
                  try
                  {
                    ELLE_TRACE_SCOPE("%s: inspect disk blocks for rebalancing",
                                     this);
                    for (auto const& address: this->storage()->list())
                    {
                      elle::reactor::sleep(100ms);
                      try
                      {
                        auto b = this->_load(address);
                        if (b.paxos)
                        {
                          auto quorum = this->_quorums.find(address);
                          ELLE_ASSERT(quorum != this->_quorums.end());
                          if (quorum->replication_factor() >= this->_factor)
                            this->_addresses.erase(address);
                          else
                            ELLE_DEBUG("%f is under-replicated", address);
                        }
                      }
                      catch (MissingBlock const&)
                      {
                        // Block was deleted in the meantime (right?).
                      }
                    }
                  }
                  catch (elle::Error const& e)
                  {
                    ELLE_ERR("disk rebalancer inspector exited: %s", e);
                  }
                }));
        }

        void
        Paxos::LocalPeer::_cleanup()
        {
          this->_rebalance_inspector.reset();
          this->_rebalance_thread.terminate_now();
          this->_evict_threads.clear();
          Super::_cleanup();
        }

        BlockOrPaxos
        Paxos::LocalPeer::_load(Address address)
        {
          if (auto decision = elle::find(this->_addresses, address))
          {
            this->_addresses.modify(decision, [&](DecisionEntry& de)
              {
                de.use = elle::Clock::now();
              });
            return BlockOrPaxos{decision->decision.get()};
          }
          else
          {
            ELLE_TRACE_SCOPE("%s: load %f from storage", *this, address);
            auto buffer = this->storage()->get(address);
            auto const context = [&]
              {
                auto res = elle::serialization::Context{};
                res.set<Doughnut*>(&this->doughnut());
                res.set<elle::Version>(
                  elle_serialization_version(this->doughnut().version()));
                return res;
              }();
            auto stored =
              elle::serialization::binary::deserialize<BlockOrPaxos>(
                buffer, true, context);
            if (stored.block)
            {
              if (this->_rebalance_auto_expand)
              {
                ELLE_LOG_COMPONENT(
                  "memo.model.doughnut.consensus.Paxos.rebalance");
                PaxosServer::Quorum q;
                if (!elle::contains(this->_quorums, address))
                {
                  for (auto wpeer: this->doughnut().overlay()->lookup(
                         address, this->_factor))
                  {
                    if (auto peer = wpeer.lock())
                      q.insert(peer->id());
                    elle::With<elle::reactor::Thread::NonInterruptible>() << [&] {
                        wpeer.reset();
                    };
                  }
                  this->_cache(address, true, q);
                  if (signed(q.size()) < this->_factor)
                  {
                    ELLE_DUMP("schedule %f for rebalancing after load",
                              address);
                    this->_rebalancable.emplace(address, false);
                  }
                }
              }
              return stored;
            }
            else if (stored.paxos)
            {
              auto decision = this->_load_paxos(address,
                std::move(*stored.paxos));
              ELLE_DEBUG("%s: reloaded %f with state %s", this, address,
                decision->paxos);
              return BlockOrPaxos{decision.get()};
            }
            else
              ELLE_ABORT("no block and no paxos?");
          }
        }

        std::shared_ptr<Paxos::LocalPeer::Decision>
        Paxos::LocalPeer::_load_paxos(
          Address address,
          boost::optional<PaxosServer::Quorum> peers,
          std::shared_ptr<blocks::Block> value)
        {
          try
          {
            if (auto decision = elle::find(this->_addresses, address))
            {
              this->_addresses.modify(decision, [&](DecisionEntry& de)
              {
                de.use = elle::Clock::now();
              });
              return decision->decision;
            }
            auto res = this->_load(address);
            if (res.block)
              elle::err("immutable block found when paxos was expected: %s",
                        address);
            else
              return ELLE_ENFORCE(elle::find(this->_addresses, address))->decision;
          }
          catch (silo::MissingKey const& e)
          {
            if (peers)
            {
              ELLE_TRACE("%s: create paxos for %f", this, address);
              auto version =
                elle_serialization_version(this->doughnut().version());
              auto v = [&] () -> boost::optional<std::shared_ptr<blocks::Block>>
                {
                  if (value)
                    return value;
                  else
                    return boost::none;
                }();
              return this->_load_paxos(
                address,
                Decision(
                  PaxosServer(this->id(), *peers, std::move(v), version)));
            }
            else
            {
              ELLE_TRACE("%s: missingkey reloading decision for %f",
                         this, address);
              throw MissingBlock(e.key());
            }
          }
        }

        std::shared_ptr<Paxos::LocalPeer::Decision>
        Paxos::LocalPeer::_load_paxos(Address address,
                                      Paxos::LocalPeer::Decision decision)
        {
          auto const& quorum = decision.paxos.current_quorum();
          this->_cache(address, false, quorum);
          if (this->_rebalance_auto_expand &&
              decision.paxos.current_value() &&
              signed(quorum.size()) < this->_factor)
          {
            ELLE_DUMP("schedule %f for rebalancing after load", address);
            this->_rebalancable.emplace(address, false);
          }
          auto ir = this->_addresses.insert(DecisionEntry{
              address,
              elle::Clock::now(),
              std::make_shared<Decision>(std::move(decision))
          });
          ELLE_ASSERT(ir.second);
          auto res = ir.first->decision;
          auto it = this->_addresses.get<1>().begin();
          for (int i = 0;
               i < signed(this->_addresses.size()) - this->_max_addresses_size;
               ++i)
            // Don't unload blocks if they are in use, as they could be reloaded
            // into _addresses in the meantime and be duplicated, entailing a
            // local split brain.
            if (it->decision.use_count() == 1)
            {
              ELLE_DEBUG(
                "dropping cache entry for %f with use index %s at state %s",
                it->address, it->use, it->decision->paxos);
              it = this->_addresses.get<1>().erase(it);
            }
          return res;
        }

        void
        Paxos::LocalPeer::_cache(Address address, bool immutable, Quorum quorum)
        {
          if (auto repartition = elle::find(this->_quorums, address))
          {
            for (auto const& n: repartition->quorum)
              this->_node_blocks.erase(NodeBlock(n, address));
            this->_quorums.erase(repartition);
          }
          this->_quorums.emplace(address, immutable, quorum);
          for (auto const& n: quorum)
            this->_node_blocks.emplace(n, address);
        }

        void
        Paxos::LocalPeer::_discovered(model::Address id)
        {
          this->_nodes.emplace(id);
          this->_node_timeouts.erase(id);
          if (this->_rebalance_auto_expand)
            this->_rebalancable.emplace(id, true);
        }

        void
        Paxos::LocalPeer::_disappeared(model::Address id)
        {
          if (this->_nodes.erase(id))
            this->_disappeared_schedule_eviction(id);
        }

        void
        Paxos::LocalPeer::_disappeared_schedule_eviction(model::Address id)
        {
          ELLE_TRACE("%s: node %f disappeared, evict in %s",
                     this, id, this->_node_timeout);
          auto it = this->_node_timeouts.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(id),
            std::forward_as_tuple(elle::reactor::scheduler().io_service()));
          it.first->second.cancel();
          it.first->second.expires_from_now(this->_node_timeout);
          it.first->second.async_wait(
            [this, id] (const boost::system::error_code& error)
            {
              if (!error)
                this->_evict_threads.emplace_back(
                  new elle::reactor::Thread(
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
          ELLE_LOG_COMPONENT(
            "memo.model.doughnut.consensus.Paxos.rebalance");
          ELLE_TRACE_SCOPE("%s: evict node %f", this, lost_id);
          auto range = elle::equal_range(
            this->_node_blocks.get<by_node>(), lost_id);
          // The addresses of the blocks that the disappeared host kept.
          // FIXME: move the range.
          auto blocks = elle::make_vector(range,
            [] (NodeBlock const& nb) { return nb.block; });
          for (auto address: blocks)
          {
            ELLE_TRACE_SCOPE("%s: evict %f from %f quorum",
                             this, lost_id, address);
            auto block = this->_load(address);
            if (block.paxos)
            {
              auto& decision = *block.paxos;
              auto q = decision.paxos.current_quorum();
              while (true)
              {
                try
                {
                  // Contact the original quorum, *before* the removal
                  // of lost_id.
                  auto client =
                    Paxos::PaxosClient(
                      this->doughnut().id(),
                      Details::lookup_nodes(this->_paxos.doughnut(),
                                            q, address));
                  if (q.erase(lost_id))
                  {
                    client.choose(decision.paxos.current_version() + 1, q);
                    ELLE_TRACE("%s: evicted %f from %f quorum",
                               this, lost_id, address);
                  }
                  break;
                }
                catch (Paxos::PaxosServer::WrongQuorum const& e)
                {
                  q = e.expected();
                }
                catch (elle::Error const& e)
                {
                  ELLE_TRACE("%s: eviction of %s failed: %s", this, address, e);
                  break;
                }
              }
            }
            else
            {
              auto const addr = block.block->address();
              auto it = this->_quorums.find(addr);
              ELLE_ASSERT(it != this->_quorums.end());
              auto q = it->quorum;
              if (q.erase(lost_id))
              {
                this->_cache(addr, true, q);
                if (signed(q.size()) < this->_factor)
                {
                  ELLE_DUMP("schedule %f for rebalancing after eviction",
                            addr);
                  this->_rebalancable.emplace(block.block->address(), false);
                }
              }
            }
          }
        }

        void
        Paxos::LocalPeer::_rebalance()
        {
          ELLE_LOG_COMPONENT(
            "memo.model.doughnut.consensus.Paxos.rebalance");
          while (true)
          {
            auto elt = this->_rebalancable.get();
            auto address = elt.first;
            if (!elt.second)
            {
              try
              {
                ELLE_TRACE_SCOPE("%s: rebalance %f", this, address);
                auto block = this->_load(address);
                if (block.paxos)
                {
                  auto peers = Details::lookup_nodes(
                    this->_paxos.doughnut(),
                    block.paxos->paxos.current_quorum(),
                    address);
                  Paxos::PaxosClient client(
                    this->doughnut().id(), std::move(peers));
                  this->rebalance(client, address);
                }
                else
                {
                  auto it = this->_quorums.find(address);
                  if (it == this->_quorums.end())
                    // The block was deleted in the meantime.
                    continue;
                  auto q = it->quorum;
                  auto new_q =
                    this->_paxos._rebalance_extend_quorum(address, q);
                  if (new_q == q)
                  {
                    ELLE_DEBUG("unable to find any new owner for %f", address);
                    this->_under_replicated(address, q.size());
                    continue;
                  }
                  else
                    ELLE_DEBUG("rebalance from %f to %f", q, new_q);
                  if (Details::send_immutable_block(
                        this->paxos(),
                        this->doughnut().overlay()->lookup_nodes(new_q),
                        *block.block,
                        q))
                      this->_rebalanced(address);
                }
              }
              catch (MissingBlock const&)
              {
                // The block was deleted in the meantime.
                ELLE_TRACE("block %f was deleted while rebalancing", address);
              }
              catch (elle::Error const& e)
              {
                ELLE_WARN("rebalancing of %f failed: %s", address, e);
              }
            }
            else
            {
              auto test = [&] (PaxosServer::Quorum const& q)
                {
                  return signed(q.size()) < this->_factor &&
                  q.find(address) == q.end();
                };
              std::unordered_set<BlockRepartition,
                                 BlockRepartition::HashByAddress> targets;
              for (auto const& r: this->_quorums.get<1>())
              {
                if (r.replication_factor() >= this->_factor)
                  break;
                if (test(r.quorum))
                  targets.emplace(r);
              }
              if (targets.empty())
                continue;
              ELLE_TRACE_SCOPE(
                "%s: rebalance %s blocks to newly discovered peer %f",
                this, targets.size(), address);
              for (auto target: targets)
              {
                if (!elle::find(this->_nodes, address) ||
                    elle::find(this->_node_timeouts, address))
                {
                  ELLE_TRACE("%s: peer %f disappeared, stop rebalancing to it",
                             this, address);
                  break;
                }
                try
                {
                  if (target.immutable)
                  {
                    auto const quorum_current =
                      ELLE_ENFORCE(elle::find(this->_quorums,
                                              target.address))->quorum;
                    auto const quorum_new = [&]
                      {
                        auto q = quorum_current;
                        q.insert(address);
                        return q;
                      }();
                    auto b = this->_load(target.address);
                    ELLE_ASSERT(b.block);
                    if (Details::send_immutable_block(
                          this->paxos(),
                          this->doughnut().overlay()->lookup_nodes(quorum_new),
                          *b.block,
                          quorum_current))
                    {
                      ELLE_TRACE("successfully duplicated %f to %f",
                                 target.address, address);
                      this->_rebalanced(target.address);
                    }
                  }
                  else
                  {
                    auto it = this->_addresses.find(target.address);
                    if (it == this->_addresses.end())
                      // The block was deleted in the meantime.
                      continue;
                    auto quorum = it->decision->paxos.current_quorum();
                    // We can't actually rebalance this block, under_represented
                    // was wrong. Don't think this can happen but better safe
                    // than sorry.
                    if (!test(quorum))
                      continue;
                    ELLE_DEBUG("elect new quorum")
                    {
                      PaxosClient c(
                        this->doughnut().id(),
                        Details::lookup_nodes(
                          this->doughnut(), quorum, target.address));
                      auto latest = this->paxos()._latest(c, address);
                      auto new_q = [address, quorum, factor = this->_factor]
                        (PaxosServer::Quorum q)
                        {
                          if (signed(quorum.size()) == factor)
                            ELLE_TRACE(
                              "someone else rebalanced to a sufficient quorum");
                          else
                            q.insert(address);
                          return q;
                        };
                      this->paxos()._rebalance(
                        c, target.address, new_q, latest);
                    }
                  }
                }
                catch (elle::Error const& e)
                {
                  ELLE_WARN("rebalancing of %f failed: %s", target.address, e);
                }
              }
            }
          }
        }

        bool
        Paxos::LocalPeer::rebalance(PaxosClient& client, Address address)
        {
          return this->_paxos._rebalance(client, address);
        }

        Paxos::PaxosServer::Response
        Paxos::LocalPeer::propose(PaxosServer::Quorum const& peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p,
                                  bool insert)
        {
          ELLE_TRACE_SCOPE("%s: get proposal at %f: %s%s",
                           *this, address, p, insert ? " (insert)" : "");
          auto decision = this->_load_paxos(
            address, insert ? boost::optional<PaxosServer::Quorum>(peers)
                            : boost::optional<PaxosServer::Quorum>());
          auto res = decision->paxos.propose(peers, p);
          auto data = BlockOrPaxos{decision.get()};
          this->storage()->set(
            address,
            elle::serialization::binary::serialize(data,
                                                   this->doughnut().version()),
            true, true);
          return res;
        }

        bool
        Paxos::LocalPeer::reconcile(Address address)
        {
          ELLE_TRACE_SCOPE("%s: reconcile %f", this, address);
          if (address.mutable_block())
          {
            try
            {
              auto decision = this->_load_paxos(address);
              auto& dht = this->paxos().doughnut();
              auto peers = Details::lookup_nodes(
                dht,
                decision->paxos.current_quorum(),
                address);
              auto client = Paxos::PaxosClient(address, std::move(peers));
              client.state();
            }
            catch (MissingBlock const&)
            {
              ELLE_LOG("%s: reconciliation remove trailing block %f", \
                       this, address);
              try
              {
                this->_remove(address);
              }
              catch (MissingBlock const&)
              {
                ELLE_TRACE("%s: block %f disappeared while reconciling",
                           this, address);
              }
              return true;
            }
            return false;
          }
          else
          {
            ELLE_WARN("%s: reconcile called on immutable block %f",
                      this, address);
            return false;
          }
        }

        Paxos::PaxosClient::Proposal
        Paxos::LocalPeer::accept(PaxosServer::Quorum const& peers,
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
              if (auto res = block->validate(this->doughnut(), true)); else
                throw ValidationFailed(res.reason());
          }
          auto decision = this->_load_paxos(address);
          auto& paxos = decision->paxos;
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
            auto data = BlockOrPaxos(decision.get());
            this->storage()->set(
              address,
              elle::serialization::binary::serialize(
                data, this->doughnut().version()),
              true, true);
          }
          if (block)
            this->on_store()(*block);
          return res;
        }

        void
        Paxos::LocalPeer::confirm(PaxosServer::Quorum const& peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p)
        {
          BENCH("confirm.local");
          ELLE_TRACE_SCOPE("%s: confirm %f at proposal %s",
                           *this, address, p);
          auto block = [&] {
            try
            {
              return BlockOrPaxos{this->_load(address)};
            }
            catch (silo::MissingKey const& k)
            {
              throw MissingBlock(k.key());
            }
          }();
          if (block.paxos)
          {
            auto& decision = *block.paxos;
            bool had_value = bool(decision.paxos.current_value());
            decision.paxos.confirm(peers, p);
            ELLE_DEBUG("store confirmed paxos")
            {
              auto data = BlockOrPaxos(&decision);
              auto ser = [&]
              {
                BENCH("confirm.storage");
                auto res = elle::serialization::binary::serialize(
                  data, this->doughnut().version());
                return res;
              }();
              this->storage()->set(address, ser, true, true);
            }
            if (auto quorum = [&] () -> boost::optional<Quorum>
              {
                if (!had_value)
                  return decision.paxos.current_quorum();
                else if (auto& accepted = decision.paxos.state()->accepted)
                {
                  if (auto res = accepted->value.template is<Quorum>())
                    return *res;
                  else
                    return boost::none;
                }
                else
                  return boost::none;
              }())
            {
              if (!contains(*quorum, this->doughnut().id()))
                ELLE_TRACE("%s: evicted from %f quorum", this, address)
                  this->_remove(address);
              else
              {
                this->_cache(address, false, *quorum);
                if (this->_rebalance_auto_expand &&
                    decision.paxos.current_value() &&
                    signed(quorum->size()) < this->_factor)
                {
                  ELLE_DEBUG("schedule %f for rebalancing after confirmation",
                             address);
                  this->_rebalancable.emplace(address, false);
                }
              }
            }
          }
          else
          {
            ELLE_DEBUG("confirm %f is stored on %f", address, peers);
            this->_cache(address, true, peers);
            if (this->_rebalance_auto_expand &&
                signed(peers.size()) < this->_factor)
            {
              ELLE_DUMP("schedule %f for rebalancing after confirmation",
                        address);
              this->_rebalancable.emplace(address, false);
            }
          }
        }

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::LocalPeer::get(PaxosServer::Quorum const& peers,
                              Address address,
                              boost::optional<int> local_version)
        {
          ELLE_TRACE_SCOPE("%s: get %f from %f", *this, address, peers);
          auto res = this->_load_paxos(address)->paxos.get(peers);
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
        Paxos::LocalPeer::propagate(PaxosServer::Quorum  q,
                                    std::shared_ptr<blocks::Block> block,
                                    Paxos::PaxosClient::Proposal p)
        {
          if (!elle::find(this->_addresses, block->address()))
          {
            ELLE_TRACE_SCOPE("%s: propagate %f on %f at %f",
                             this, block->address(), q, p);
            auto decision = this->_load_paxos(block->address(), q, block);
            decision->paxos.propose(q, p);
            decision->paxos.accept(q, p, q);
            decision->paxos.confirm(q, p);
            auto data = BlockOrPaxos(decision.get());
            this->storage()->set(
              block->address(),
              elle::serialization::binary::serialize(
                data, this->doughnut().version()),
              true, true);
            this->on_store()(*block);
          }
          else
            ELLE_TRACE("%s: %f is already propagated", this, block->address());
        }

        void
        Paxos::LocalPeer::_register_rpcs(Connection& connection)
        {
          auto& rpcs = connection.rpcs();
          Local::_register_rpcs(connection);
          namespace ph = std::placeholders;
          if (this->doughnut().version() >= elle::Version(0, 9, 0))
            rpcs.add(
              "propose",
              [this, &rpcs](PaxosServer::Quorum q,
                            Address a,
                            Paxos::PaxosClient::Proposal const& p,
                            bool insert)
              {
                this->_require_auth(rpcs, true);
                return this->propose(std::move(q), a, p, insert);
              });
          else
            rpcs.add(
              "propose",
              [this, &rpcs](PaxosServer::Quorum q,
                            Address a,
                            Paxos::PaxosClient::Proposal const& p)
              {
                this->_require_auth(rpcs, true);
                return this->propose(std::move(q), a, p, true);
              });
          if (this->doughnut().version() < elle::Version(0, 5, 0))
            rpcs.add(
              "accept",
              [this, &rpcs] (PaxosServer::Quorum q, Address a,
                             Paxos::PaxosClient::Proposal const& p,
                             std::shared_ptr<blocks::Block> const& b)
               -> Paxos::PaxosClient::Proposal
              {
                this->_require_auth(rpcs, true);
                return this->accept(q, a, p, std::move(b));
              });
          else
            rpcs.add(
              "accept",
              [this, &rpcs](PaxosServer::Quorum q,
                            Address a,
                            Paxos::PaxosClient::Proposal const& p,
                            Value const& value)
               {
                 this->_require_auth(rpcs, true);
                 return this->accept(std::move(q), a, p, value);
               });
          rpcs.add(
            "confirm",
            [this](PaxosServer::Quorum q, Address a,
                   Paxos::PaxosClient::Proposal const& p)
            {
              return this->confirm(q, a, p);
            });
          rpcs.add(
            "get",
            [this](PaxosServer::Quorum q, Address a,
                   boost::optional<int> v)
            {
              return this->get(q, a, v);
            });
            rpcs.add(
              "reconcile",
              [this, &rpcs] (Address a)
              {
                this->_require_auth(rpcs, true);
                return this->reconcile(std::move(a));
              });
          if (this->doughnut().version() >= elle::Version(0, 9, 0))
            rpcs.add(
              "propagate",
              [this, &rpcs](PaxosServer::Quorum q,
                            std::shared_ptr<blocks::Block> block,
                            Paxos::PaxosClient::Proposal p)
              {
                this->_require_auth(rpcs, true);
                return this->propagate(
                  std::move(q), std::move(block), std::move(p));
              });
        }

        std::unique_ptr<blocks::Block>
        Paxos::LocalPeer::_fetch(Address address,
                                 boost::optional<int> local_version) const
        {
          elle::serialization::Context context;
          context.set<Doughnut*>(&this->doughnut());
          context.set<elle::Version>(
            elle_serialization_version(this->doughnut().version()));
          auto data =
            elle::serialization::binary::deserialize<BlockOrPaxos>(
              this->storage()->get(address), true, context);
          if (!data.block)
          {
            ELLE_TRACE("%s: plain fetch called on mutable block", *this);
            elle::err("plain fetch called on mutable block %f", address);
          }
          return std::unique_ptr<blocks::Block>(data.block.release());
        }

        void
        Paxos::LocalPeer::store(blocks::Block const& block, StoreMode mode)
        {
          ELLE_TRACE_SCOPE("%s: store %f", *this, block);
          ELLE_DEBUG("%s: validate block", *this)
            if (auto res = block.validate(this->doughnut(), true)); else
              throw ValidationFailed(res.reason());
          if (!dynamic_cast<blocks::ImmutableBlock const*>(&block))
            throw ValidationFailed("bypassing Paxos for a mutable block");
          // validate with previous version
          try
          {
            auto previous_buffer = this->storage()->get(block.address());
            elle::IOStream s(previous_buffer.istreambuf());
            typename elle::serialization::binary::SerializerIn input(s);
            input.set_context<Doughnut*>(&this->doughnut());
            input.set_context<elle::Version>(
              elle_serialization_version(this->doughnut().version()));
            auto stored = input.deserialize<BlockOrPaxos>();
            if (!stored.block)
              throw ValidationFailed(
                elle::sprintf("storing immutable block on mutable block %f",
                              block.address()));
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
          catch (silo::MissingKey const&)
          {}
          elle::Buffer data =
            [&]
            {
              auto b = BlockOrPaxos(const_cast<blocks::Block&>(block));
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
          ELLE_TRACE_SCOPE("%s: remove %f", this, address);
          if (this->doughnut().version() >= elle::Version(0, 4, 0))
          {
            try
            {
              auto b = this->_load(address);
              if (b.paxos)
              {
                auto& paxos = b.paxos->paxos;
                if (auto highest = paxos.current_value())
                {
                  auto& v =
                    highest->value.get<std::shared_ptr<blocks::Block>>();
                  auto valres = v->validate_remove(this->doughnut(), rs);
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
              {
                auto& previous = *b.block;
                auto valres = previous.validate_remove(this->doughnut(), rs);
                if (!valres)
                  if (valres.conflict())
                    throw Conflict(valres.reason(), previous.clone());
                  else
                    throw ValidationFailed(valres.reason());
              }
            }
            catch (silo::MissingKey const& k)
            {
              throw MissingBlock(k.key());
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
          catch (silo::MissingKey const& k)
          {
            throw MissingBlock(k.key());
          }
          this->_node_blocks.get<by_block>().erase(address);
          this->on_remove()(address);
          this->_addresses.erase(address);
        }

        static
        std::shared_ptr<blocks::Block>
        resolve(blocks::Block& b,
                blocks::Block& newest,
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
            auto resolved = (*resolver)(b, newest);
            if (resolved)
              return std::shared_ptr<blocks::Block>(resolved.release());
            else
            {
              ELLE_TRACE("resolution failed");
              // FIXME: useless clone, find a way to steal ownership
              throw memo::model::Conflict(
                "Paxos chose a different value", newest.clone());
            }
          }
          else
          {
            ELLE_TRACE("chosen block differs, signal conflict");
            // FIXME: useless clone, find a way to steal ownership
            throw memo::model::Conflict(
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
          auto owners = [&]
          {
            switch (mode)
            {
              case STORE_INSERT:
                return this->doughnut().overlay()->allocate(
                  b->address(), this->_factor);
              case STORE_UPDATE:
                return this->doughnut().overlay()->lookup(
                  b->address(), this->_factor, false);
              default:
              elle::unreachable();
            }
          }();
          if (dynamic_cast<blocks::MutableBlock*>(b.get()))
          {
            auto peers = Details::Peers();
            PaxosServer::Quorum peers_id;
            // FIXME: This voids the "query on the fly" optimization
            // as it forces resolution of all peers to get their
            // id. Any other way?
            for (auto wpeer: owners)
              if (auto peer = wpeer.lock())
              {
                peers_id.insert(peer->id());
                peers.emplace_back(
                  std::make_unique<PaxosPeer>(
                    wpeer, b->address(), boost::none, mode == STORE_INSERT));
              }
              else
                ELLE_WARN("%s: peer was deleted while storing", this);
            if (peers.empty())
              elle::err("no peer available for %s of %f",
                        mode == STORE_INSERT ? "insertion" : "update",
                        b->address());
            ELLE_DEBUG("owners: %f", peers);
            // FIXME: client is persisted on conflict resolution, hence the
            // round number is kept and won't start at 0.
            // Keep retrying with new quorums
            while (true)
            {
              try
              {
                auto client = Paxos::PaxosClient(
                  this->doughnut().id(), std::move(peers));
                // Keep resolving conflicts and retrying
                while (true)
                {
                  auto mb = dynamic_cast<blocks::MutableBlock*>(b.get());
                  auto version = mb->version();
                  auto const chosen = [&]
                    {
                      ELLE_DEBUG("run Paxos for version %s", version)
                        return client.choose(version, b);
                    }();
                  if (chosen)
                  {
                    if (chosen->is<PaxosServer::Quorum>())
                    {
                      auto const& q = chosen->get<PaxosServer::Quorum>();
                      ELLE_DEBUG_SCOPE("Paxos elected another quorum: %f", q);
                      b->seal(chosen.proposal().version + 1);
                      throw Paxos::PaxosServer::WrongQuorum(
                        q, peers_id, chosen.proposal());
                    }
                    else
                    {
                      auto block =
                        chosen->get<std::shared_ptr<blocks::Block>>();
                      if (auto* mb = dynamic_cast<blocks::MutableBlock*>(block.get()))
                        mb->seal_version(chosen.proposal().version + 1);
                      if (auto* mb = dynamic_cast<blocks::MutableBlock*>(b.get()))
                        mb->seal_version(chosen.proposal().version + 1);
                      if (!(b = resolve(*b, *block, resolver.get())))
                        break;
                      ELLE_DEBUG("seal resolved block")
                        b->seal();
                    }
                  }
                  else
                    break;
                }
              }
              catch (Paxos::PaxosServer::WrongQuorum const& e)
              {
                ELLE_TRACE("%s", e.what());
                peers = Details::lookup_nodes(
                  this->doughnut(), e.expected(), b->address());
                peers_id.clear();
                for (auto const& peer: peers)
                  peers_id.insert(static_cast<PaxosPeer&>(*peer).id());
                continue;
              }
              break;
            }
          }
          else if (!Details::send_immutable_block(
                     *this, owners, *b, PaxosClient::Quorum()))
            elle::err("no peer available for insertion of %f", b->address());
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

        void
        Paxos::_fetch(std::vector<AddressVersion> const& addresses,
                      ReceiveBlock res)
        {
          BENCH("multi_fetch");
          if (this->doughnut().version() < elle::Version(0, 5, 0))
          {
            for (auto av: addresses)
            {
              try
              {
                auto block = this->_fetch(av.first, av.second);
                res(av.first, std::move(block), {});
              }
              catch (elle::Error const& e)
              {
                res(av.first, {}, std::make_exception_ptr(e));
              }
            }
            return;
          }
          ELLE_DEBUG("querying %s addresses", addresses.size());
          auto hits = this->doughnut().overlay()->lookup(
            elle::make_vector(addresses,
                              [](auto const& a){ return a.first; }),
            this->_factor);
          auto versions = std::unordered_map<Address, boost::optional<int>>{};
          for (auto a: addresses)
            versions[a.first] = a.second;
          auto peers = std::unordered_map<Address, Details::Peers>();
          for (auto r: hits)
            peers[r.first].emplace_back(
              std::make_unique<PaxosPeer>(
                r.second, r.first, versions.at(r.first), false));
          elle::reactor::for_each_parallel(
            peers,
            [&] (std::pair<Address const, Details::Peers>& p)
            {
              try
              {
                auto block = Details::_fetch(
                  *this, p.first, std::move(p.second), versions.at(p.first));
                res(p.first, std::move(block), {});
              }
              catch (elle::Error const& e)
              {
                res(p.first, {}, std::current_exception());
              }
            },
            elle::print("multifetch"));
        }

        std::unique_ptr<blocks::Block>
        Paxos::_fetch(Address address, boost::optional<int> local_version)
        {
          if (this->doughnut().version() < elle::Version(0, 5, 0))
          {
            auto peers =
              this->doughnut().overlay()->lookup(address, this->_factor);
            return fetch_from_members(peers, address, std::move(local_version));
          }
          auto peers = Details::_peers(*this, address, local_version);
          return Details::_fetch(
            *this, address, std::move(peers), local_version);
        }

        Paxos::PaxosClient
        Paxos::_client(Address const& address)
        {
          return Paxos::PaxosClient(
            this->doughnut().id(), Details::_peers(*this, address));
        }

        Paxos::PaxosClient::State
        Paxos::_latest(PaxosClient& client, Address address)
        {
          while (true)
            try
            {
              return client.state();
            }
            catch (Paxos::PaxosServer::WrongQuorum const& e)
            {
              client.peers(
                Details::lookup_nodes(this->doughnut(), e.expected(), address));
            }
        }

        bool
        Paxos::rebalance(Address address)
        {
          ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Paxos.rebalance");
          ELLE_TRACE_SCOPE("%s: rebalance %f", *this, address);
          auto client = this->_client(address);
          auto local =
            std::static_pointer_cast<LocalPeer>(this->doughnut().local());
          return local->rebalance(client, address);
        }

        // FIXME: rebalancers will re-lookup the node ids, which sucks
        Paxos::PaxosServer::Quorum
        Paxos::_rebalance_extend_quorum(Address address,
                                        PaxosServer::Quorum q)
        {
          // Make sure we didn't lose a previous owner because of the overlay
          // failing to look it up.
          PaxosServer::Quorum new_q(q);
          for (auto const& wowner:
                 this->doughnut().overlay()->allocate(address, this->_factor))
          {
            if (signed(new_q.size()) >= this->_factor)
              break;
            if (auto owner = wowner.lock())
              new_q.emplace(owner->id());
          }
          return new_q;
        }

        bool
        Paxos::rebalance(Address address, PaxosClient::Quorum const& ids)
        {
          ELLE_LOG_COMPONENT(
            "infinit.model.doughnut.consensus.Paxos.rebalance");
          ELLE_TRACE_SCOPE("%s: rebalance %f to %f", *this, address, ids);
          auto client = this->_client(address);
          auto latest = this->_latest(client, address);
          auto new_q = [ids] (PaxosServer::Quorum)
            {
              return ids;
            };
          return this->_rebalance(client, address, new_q, latest);
        }

        bool
        Paxos::_rebalance(PaxosClient& client, Address address)
        {
          ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Paxos.rebalance");
          ELLE_ASSERT_GTE(this->doughnut().version(), elle::Version(0, 5, 0));
          auto latest = this->_latest(client, address);
          // FIXME: handle immutable block errors
          ELLE_DEBUG("quorum: %f", latest.quorum);
          if (signed(latest.quorum.size()) == this->_factor)
          {
            ELLE_TRACE("block is already well balanced (%s replicas)",
                       this->_factor);
            return false;
          }
          auto new_q = [address, this] (PaxosServer::Quorum q)
            {
              auto res = this->_rebalance_extend_quorum(address, q);
              if (res == q)
                ELLE_TRACE("unable to find any new owner");
              return res;
            };
          return this->_rebalance(client, address, new_q, latest);
        }

        bool
        Paxos::_rebalance(
          PaxosClient& client,
          Address address,
          std::function<PaxosClient::Quorum (PaxosClient::Quorum)> make_quorum,
          PaxosClient::State const& state)
        {
          ELLE_LOG_COMPONENT("memo.model.doughnut.consensus.Paxos.rebalance");
          auto new_quorum = make_quorum(state.quorum);
          if (new_quorum == state.quorum)
            return false;
          auto const propagate = [&] (PaxosClient::Proposal const& p)
            {
              auto local =
                std::static_pointer_cast<LocalPeer>(this->doughnut().local());
              Details::_propagate(*local, address, p, state.quorum, new_quorum);
              local->_rebalanced(address);
              return true;
            };
          std::unique_ptr<PaxosClient> replace;
          int version = state.proposal ? state.proposal->version : -1;
          while (true)
          {
            ELLE_DEBUG("rebalance block to: %f", new_quorum)
            try
            {
              // FIXME: version is the last *value* version, there could have
              // been a quorum since then in which case this will fail.
              if (auto conflict =
                  (replace ? *replace : client).choose(version + 1, new_quorum))
              {
                // FIXME: Retry balancing.
                // FIXME: We should still try block propagation in the case that
                // "someone else" failed to perform it.
                if (conflict->is<PaxosServer::Quorum>())
                {
                  auto quorum = conflict->get<PaxosServer::Quorum>();
                  if (quorum == new_quorum)
                  {
                    ELLE_TRACE(
                      "conflicted rebalancing to the quorum we picked");
                    return propagate(conflict.proposal());
                  }
                  else if (signed(quorum.size()) == this->_factor)
                  {
                    new_quorum = make_quorum(quorum);
                    if (new_quorum == quorum)
                    {
                      ELLE_TRACE(
                        "conflicted rebalancing to a satisfying quorum");
                      return propagate(conflict.proposal());
                    }
                    else
                    {
                      ELLE_TRACE(
                        "conflicted rebalancing to an non-satisfying quorum");
                      ++version;
                      replace.reset(
                        new Paxos::PaxosClient(
                          this->doughnut().id(),
                          Details::lookup_nodes(
                            this->doughnut(), quorum, address)));
                      continue;
                    }
                  }
                }
                else
                {
                  // FIXME: does this then skip versions, possibly hiding a
                  // different and potentially better quorum pick?
                  ELLE_TRACE("someone else picked a value while we rebalanced");
                  ++version;
                  continue;
                }
              }
              else
              {
                ELLE_TRACE("successfully rebalanced to %s nodes at version %s",
                           new_quorum.size(), version + 1);
                return propagate(conflict.proposal());
              }
            }
            catch (Paxos::PaxosServer::WrongQuorum const& e)
            {
              replace.reset(
                new Paxos::PaxosClient(
                  this->doughnut().id(),
                  Details::lookup_nodes(
                    this->doughnut(), e.expected(), address)));
            }
            catch (elle::Error const&)
            {
              ELLE_WARN("rebalancing failed: %s", elle::exception_string());
              return false;
            }
          }
        }

        void
        Paxos::_resign()
        {
          ELLE_LOG_COMPONENT(
            "memo.model.doughnut.consensus.Paxos.rebalance");
          auto local = this->doughnut().local();
          if (!local)
            return;
          auto backoff = elle::reactor::Backoff(10ms, 10s);
          auto paxos = std::static_pointer_cast<LocalPeer>(local);
          for (bool failed = false, done = false; !done;)
          {
            done = true;
            auto range = elle::equal_range(
              paxos->node_blocks().get<LocalPeer::by_node>(),
              this->doughnut().id());
            // FIXME: move the range
            for (auto nb: elle::make_vector(range))
            {
              if (failed)
              {
                failed = false;
                backoff.backoff();
              }
              auto address = nb.block;
              if (!address.mutable_block())
                continue;
              done = false;
              ELLE_TRACE_SCOPE("rebalance %f out", address);
              auto new_quorum = [id = this->doughnut().id()]
                (PaxosServer::Quorum q)
                {
                  q.erase(id);
                  return q;
                };
              try
              {
                auto client = this->_client(address);
                auto latest = this->_latest(client, address);
                if (!this->_rebalance(client, address, new_quorum, latest))
                {
                  failed = true;
                  ELLE_WARN("%f: unable to rebalance %f", this, address);
                }
              }
              catch (elle::Error const& e)
              {
                failed = true;
                ELLE_WARN("%f: unable to rebalance %f: %f", this, address, e);
              }
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

        /*-----.
        | Stat |
        `-----*/
#if 0
        using Hits =
          std::unordered_map<std::string, boost::optional<Hit>>;

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
#endif

        std::unique_ptr<Consensus::Stat>
        Paxos::stat(Address const& address)
        {
          ELLE_TRACE_SCOPE("%s: stat %s", *this, address);
          // ELLE_ASSERT_GTE(
          //   this->doughnut().version(), elle::Version(0, 5, 0));
          // auto hits = _Details::_multifetch_paxos(*this, address);
          // auto peers =
          //   this->_owners(address, this->_factor, overlay::OP_FETCH);
          // Hits stat_hits;
          // for (auto& hit: hits)
          // {
          //   auto node = elle::sprintf("%s", hit.node());
          //   stat_hits.emplace(node, std::move(hit));
          // }
          // return std::make_unique<PaxosStat>(std::move(stat_hits));
          return Super::stat(address);
        }

        /*-----------.
        | Monitoring |
        `-----------*/

        elle::json::Object
        Paxos::redundancy()
        {
          return {
            { "desired_factor", static_cast<float>(this->factor()) },
            { "type", "replication" },
          };
        }

        elle::json::Object
        Paxos::stats()
        {
          return {
            {"type", "paxos"},
            {"node_timeout", elle::sprintf("%s", this->node_timeout())},
          };
        }

        /*--------------.
        | Configuration |
        `--------------*/

        Paxos::Configuration::Configuration(
          int replication_factor,
          Duration node_timeout)
          : consensus::Configuration()
          , _replication_factor(replication_factor)
          , _node_timeout(node_timeout)
          , _rebalance_auto_expand(true)
          , _rebalance_inspect(true)
        {}

        std::unique_ptr<Consensus>
        Paxos::Configuration::make(model::doughnut::Doughnut& dht)
        {
          return std::make_unique<Paxos>(
            dht,
            consensus::replication_factor = this->_replication_factor,
            consensus::node_timeout = this->_node_timeout,
            consensus::rebalance_auto_expand = this->_rebalance_auto_expand,
            consensus::rebalance_inspect = this->_rebalance_inspect);
        }

        Paxos::Configuration::Configuration(
          elle::serialization::SerializerIn& s)
          : _rebalance_auto_expand(true)
          , _rebalance_inspect(true)
        {
          this->serialize(s);
        }

        void
        Paxos::Configuration::serialize(elle::serialization::Serializer& s)
        {
          consensus::Configuration::serialize(s);
          s.serialize("replication-factor", this->_replication_factor);
          try
          {
            s.serialize("eviction-delay", this->_node_timeout);
          }
          catch (elle::serialization::MissingKey const&)
          {
            ELLE_ASSERT(s.in());
            this->_node_timeout = default_node_timeout;
          }
        }

        static const elle::serialization::Hierarchy<Configuration>::
        Register<Paxos::Configuration> _register_Configuration("paxos");

        static const elle::serialization::Hierarchy<elle::Exception>::
        Register<Paxos::PaxosServer::WrongQuorum> _register_paxos_WrongQuorum(
          "athena::paxos::Server<std::shared_ptr<infinit::model::blocks::Block>, int, infinit::model::Address, infinit::model::Address>::WrongQuorum");

        static const elle::serialization::Hierarchy<elle::Exception>::
        Register<Paxos::PaxosServer::PartialState> _register_paxos_PartialState(
          "athena::paxos::Server<std::shared_ptr<infinit::model::blocks::Block>, int, infinit::model::Address, infinit::model::Address>::PartialState");
      }
    }
  }
}

namespace elle
{
  namespace athena
  {
    namespace paxos
    {
      static const elle::serialization::Hierarchy<elle::Exception>::
      Register<TooFewPeers> _register_serialization;
      static const elle::serialization::Hierarchy<elle::Exception>::
      Register<Unavailable> _register_serialization_unavailable;
    }
  }
}

ELLE_TYPE_INFO_ABBR("PaxosLocal", "consensus::Paxos::LocalPeer");
ELLE_TYPE_INFO_ABBR("PaxosRemote", "consensus::Paxos::RemotePeer");
