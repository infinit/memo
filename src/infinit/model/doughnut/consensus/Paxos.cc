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

        struct BlockOrPaxos
        {
          BlockOrPaxos(blocks::Block* b)
            : block(b)
            , paxos()
          {}

          BlockOrPaxos(Paxos::LocalPeer::Decision* p)
            : block(nullptr)
            , paxos(p)
          {}

          BlockOrPaxos(elle::serialization::SerializerIn& s)
          {
            this->serialize(s);
          }

          blocks::Block* block;
          Paxos::LocalPeer::Decision* paxos;

          void
          serialize(elle::serialization::Serializer& s)
          {
            s.serialize("block", this->block);
            s.serialize("paxos", this->paxos);
          }

          typedef infinit::serialization_tag serialization_tag;
        };

        /*-------------.
        | Construction |
        `-------------*/

        Paxos::Paxos(Doughnut& doughnut, int factor, bool lenient_fetch)
          : Super(doughnut)
          , _factor(factor)
          , _lenient_fetch(lenient_fetch)
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
            this->factor(),
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
          Peer(reactor::Generator<overlay::Overlay::Member>& members,
               Address address)
            : _members(&members)
            , _member()
            , _address(address)
          {}

          Peer(overlay::Overlay::Member member,
               Address address)
            : _members(nullptr)
            , _member(std::move(member))
            , _address(address)
          {
            ELLE_ASSERT(this->_member);
          }

          virtual
          boost::optional<Paxos::PaxosClient::Accepted>
          propose(Paxos::PaxosClient::Quorum const& q,
                  Paxos::PaxosClient::Proposal const& p)
          {
            BENCH("propose");
            auto& member = this->member();
            return network_exception_to_unavailable([&] {
              if (auto local = dynamic_cast<Paxos::LocalPeer*>(&member))
                return local->propose(
                  q, this->_address, p);
              else if (auto remote = dynamic_cast<Paxos::RemotePeer*>(&member))
                return remote->propose(
                  q, this->_address, p);
              else if (dynamic_cast<DummyPeer*>(&member))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", member);
            });
          }

          virtual
          Paxos::PaxosClient::Proposal
          accept(Paxos::PaxosClient::Quorum const& q,
                 Paxos::PaxosClient::Proposal const& p,
                 std::shared_ptr<blocks::Block> const& value)
          {
            BENCH("accept");
            auto& member = this->member();
            return network_exception_to_unavailable([&] {
              if (auto local = dynamic_cast<Paxos::LocalPeer*>(&member))
                return local->accept(
                  q, this->_address, p, value);
              else if (auto remote = dynamic_cast<Paxos::RemotePeer*>(&member))
                return remote->accept(
                  q, this->_address, p, value);
              else if (dynamic_cast<DummyPeer*>(&member))
                throw reactor::network::Exception("Peer unavailable");
              ELLE_ABORT("invalid paxos peer: %s", member);
            });
          }

          infinit::model::doughnut::Peer&
          member()
          {
            if (!this->_member)
              try
              {
                ELLE_ASSERT(this->_members);
                this->_member = this->_members->next();
              }
              catch (reactor::Generator<overlay::Overlay::Member>::End const&)
              {
                std::throw_with_nested(Unavailable());
              }
            return *this->_member;
          }

          ELLE_ATTRIBUTE(reactor::Generator<overlay::Overlay::Member>*,
                         members);
          ELLE_ATTRIBUTE(overlay::Overlay::Member, member);
          ELLE_ATTRIBUTE(Address, address);
          ELLE_ATTRIBUTE(int, version);
        };

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
                                  std::shared_ptr<blocks::Block> const& value)
        {
          return network_exception_to_unavailable([&] {
            auto accept = make_rpc<Paxos::PaxosClient::Proposal (
              PaxosServer::Quorum peers,
              Address,
              Paxos::PaxosClient::Proposal const&,
              std::shared_ptr<blocks::Block> const&)>("accept");
            accept.set_context<Doughnut*>(&this->_doughnut);
            return accept(peers, address, p, value);
          });
        }

        std::pair<Paxos::PaxosServer::Quorum,
                  std::unique_ptr<Paxos::PaxosClient::Accepted>>
        Paxos::RemotePeer::_fetch_paxos(Address address)
        {
          auto fetch = make_rpc
            <std::pair<PaxosServer::Quorum,
                       std::unique_ptr<Paxos::PaxosClient::Accepted>>(Address)>
            ("fetch_paxos");
          fetch.set_context<Doughnut*>(&this->doughnut());
          return fetch(address);
        }

        /*----------.
        | LocalPeer |
        `----------*/

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::LocalPeer::propose(PaxosServer::Quorum peers,
                                  Address address,
                                  Paxos::PaxosClient::Proposal const& p)
        {
          ELLE_TRACE_SCOPE("%s: get proposal at %s: %s",
                           *this, address, p);
          auto decision = this->_addresses.find(address);
          if (decision == this->_addresses.end())
            try
            {
              auto buffer = this->storage()->get(address);
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto stored =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  buffer, true, context);
              if (!stored.paxos)
                throw elle::Error("running Paxos on an immutable block");
              decision = this->_addresses.emplace(
                address, std::move(*stored.paxos)).first;
            }
            catch (storage::MissingKey const&)
            {
              decision = this->_addresses.emplace(
                address,
                Decision(PaxosServer(this->id(), peers))).first;
            }
          auto res = decision->second.paxos.propose(std::move(peers), p);
          this->storage()->set(
            address,
            elle::serialization::binary::serialize(
              BlockOrPaxos(&decision->second)),
            true, true);
          return res;
        }

        Paxos::PaxosClient::Proposal
        Paxos::LocalPeer::accept(PaxosServer::Quorum peers,
                                 Address address,
                                 Paxos::PaxosClient::Proposal const& p,
                                 std::shared_ptr<blocks::Block> const& value)
        {
          ELLE_TRACE_SCOPE("%s: accept at %s: %s",
                           *this, address, p);
          // FIXME: factor with validate in doughnut::Local::store
          ELLE_DEBUG("validate block")
            if (auto res = value->validate()); else
              throw ValidationFailed(res.reason());
          auto& decision = this->_addresses.at(address);
          auto& paxos = decision.paxos;
          if (auto highest = paxos.highest_accepted())
          {
            auto& val = highest->value;
            auto valres = val->validate(*value);
            if (!valres)
              throw Conflict("peer validation failed", value->clone());
          }
          auto res = paxos.accept(std::move(peers), p, value);
          {
            ELLE_DEBUG_SCOPE("store accepted paxos");
            this->storage()->set(
              address,
              elle::serialization::binary::serialize(BlockOrPaxos(&decision)),
              true, true);
          }
          on_store(*value, STORE_ANY);
          return std::move(res);
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
              Paxos::PaxosClient::Proposal const&)>
            (std::bind(&LocalPeer::propose,
                       this, ph::_1, ph::_2, ph::_3)));
          rpcs.add(
            "accept",
            std::function<
            Paxos::PaxosClient::Proposal(
              PaxosServer::Quorum, Address,
              Paxos::PaxosClient::Proposal const& p,
              std::shared_ptr<blocks::Block> const& value)>
            (std::bind(&LocalPeer::accept,
                       this, ph::_1, ph::_2, ph::_3, ph::_4)));
          rpcs.add(
            "fetch_paxos",
            std::function<std::pair<PaxosServer::Quorum,
                                   std::unique_ptr<Paxos::PaxosClient::Accepted>>
                                   (Address)>
            (std::bind(&LocalPeer::_fetch_paxos,
                       this, ph::_1)));
        }

        template <typename T>
        T&
        unconst(T const& v)
        {
          return const_cast<T&>(v);
        }

        static
        Paxos::PaxosClient::Peers
        lookup_nodes(Doughnut& dht,
                     Paxos::PaxosServer::Quorum const& q,
                     Address address)
        {
          Paxos::PaxosClient::Peers res;
          for (auto member: dht.overlay()->lookup_nodes(q))
          {
            res.push_back(
              elle::make_unique<consensus::Peer>(
                std::move(member), address));
          }
          return res;
        }

        std::pair<Paxos::PaxosServer::Quorum,
                  std::unique_ptr<Paxos::PaxosClient::Accepted>>
        Paxos::LocalPeer::_fetch_paxos(Address address)
        {
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
                return std::make_pair(PaxosServer::Quorum(),elle::make_unique<PaxosClient::Accepted>(
                  PaxosClient::Proposal(-1, -1, this->doughnut().id()),
                  std::shared_ptr<blocks::Block>(data.block)));
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
          auto highest = paxos.highest_accepted();
          if (!highest)
            throw MissingBlock(address);
          return std::make_pair(
            paxos.quorum(),
            elle::make_unique<PaxosClient::Accepted>(*highest));
        }

        std::unique_ptr<blocks::Block>
        Paxos::LocalPeer::_fetch(Address address,
                                 boost::optional<int> local_version) const
        {
          ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
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
                return std::unique_ptr<blocks::Block>(data.block);
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
          if (auto highest = paxos.highest_accepted())
          {
            auto version = highest->proposal.version;
            if (decision->second.chosen == version)
            {
              ELLE_DEBUG("return already chosen mutable block");
              return highest->value->clone();
            }
            else
            {
              ELLE_TRACE_SCOPE(
                "finalize running Paxos for version %s (last chosen %s)"
                , version, decision->second.chosen);
              auto block = highest->value;
              Paxos::PaxosClient::Peers peers =
                lookup_nodes(
                  this->doughnut(), paxos.quorum(), block->address());
              if (peers.empty())
                throw elle::Error(
                  elle::sprintf("No peer available for fetch %x", block->address()));
              Paxos::PaxosClient client(uid(this->doughnut().keys().K()),
                                        std::move(peers));
              auto chosen = client.choose(paxos.quorum(), version, block);
              // FIXME: factor with the end of doughnut::Local::store
              ELLE_DEBUG("%s: store chosen block", *this)
              unconst(decision->second).chosen = version;
              {
                this->storage()->set(
                  address,
                  elle::serialization::binary::serialize(
                    BlockOrPaxos(const_cast<Decision*>(&decision->second))),
                  true, true);
              }
              // ELLE_ASSERT(block.unique());
              // FIXME: Don't clone, it's useless, find a way to steal
              // ownership from the shared_ptr.
              return block->clone();
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
            if (auto res = block.validate()); else
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
            elle::SafeFinally cleanup([&] {
                  delete stored.block;
                  delete stored.paxos;
            });
            if (!stored.block)
              ELLE_WARN("No block, cannot validate update");
            else
            {
              auto vr = stored.block->validate(block);
              if (!vr)
                if (vr.conflict())
                  throw Conflict(vr.reason(), stored.block->clone());
                else
                  throw ValidationFailed(vr.reason());
            }
          }
          catch (storage::MissingKey const&)
          {
          }
          elle::Buffer data =
            elle::serialization::binary::serialize(
              BlockOrPaxos(const_cast<blocks::Block*>(&block)));
          this->storage()->set(block.address(), data,
                              mode == STORE_ANY || mode == STORE_INSERT,
                              mode == STORE_ANY || mode == STORE_UPDATE);
          on_store(block, mode);
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
              if (auto highest = paxos.highest_accepted())
              {
                auto& val = highest->value;
                auto valres = val->validate_remove(rs);
                ELLE_TRACE("mutable block remove validation gave %s", valres);
                if (!valres)
                  if (valres.conflict())
                    throw Conflict(valres.reason(), val->clone());
                  else
                    throw ValidationFailed(valres.reason());
              }
              else
                ELLE_WARN("No paxos accepted, cannot validate removal");
            }
            else
            { // immutable block
              auto buffer = this->storage()->get(address);
              elle::serialization::Context context;
              context.set<Doughnut*>(&this->doughnut());
              auto stored =
                elle::serialization::binary::deserialize<BlockOrPaxos>(
                  buffer, true, context);
              elle::SafeFinally cleanup([&] {
                  delete stored.block;
                  delete stored.paxos;
              });
              if (!stored.block)
                ELLE_WARN("No paxos and no block, cannot validate removal");
              else
              {
                auto previous = stored.block;
                auto valres = previous->validate_remove(rs);
                ELLE_TRACE("Immutable block remove validation gave %s", valres);
                if (!valres)
                  if (valres.conflict())
                    throw Conflict(valres.reason(), previous->clone());
                  else
                    throw ValidationFailed(valres.reason());
              }
            }
          }
          try
          {
            this->storage()->erase(address);
          }
          catch (storage::MissingKey const& k)
          {
            throw MissingBlock(k.key());
          }
          on_remove(address);
          this->_addresses.erase(address);
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
            case STORE_ANY:
              op = overlay::OP_INSERT_OR_UPDATE;
              break;
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
            // FIXME: this voids the whole "query on the fly" optimisation
            Paxos::PaxosClient::Peers peers;
            PaxosServer::Quorum peers_id;
            // FIXME: This void the "query on the fly" optimization as it
            // forces resolution of all peers to get their idea. Any other
            // way ?
            for (auto peer: owners)
            {
              peers_id.insert(peer->id());
              peers.push_back(
                elle::make_unique<Peer>(peer, b->address()));
            }
            if (peers.empty())
              throw elle::Error(
                elle::sprintf("No peer available for store %x", b->address()));
            // FIXME: client is persisted on conflict resolution, hence the
            // round number is kept and won't start at 0.
            while (true)
            {
              try
              {
                Paxos::PaxosClient client(
                  uid(this->doughnut().keys().K()), std::move(peers));
                while (true)
                {
                  auto version =
                    dynamic_cast<blocks::MutableBlock*>(b.get())->version();
                  boost::optional<std::shared_ptr<blocks::Block>> chosen;
                  ELLE_DEBUG("run Paxos for version %s", version)
                    chosen = client.choose(peers_id, version, b);
                  if (chosen && *chosen.get() != *b)
                  {
                    if (resolver)
                    {
                      ELLE_TRACE_SCOPE(
                        "chosen block differs, run conflict resolution");
                      auto block = (*resolver)(*b, *chosen.get(), mode);
                      if (block)
                      {
                        ELLE_DEBUG_SCOPE("seal resolved block");
                        block->seal();
                        b.reset(block.release());
                      }
                      else
                      {
                        ELLE_TRACE("resolution failed");
                        // FIXME: useless clone, find a way to steal ownership
                        throw infinit::model::doughnut::Conflict(
                          "Paxos chose a different value",
                          chosen.get()->clone());
                      }
                    }
                    else
                    {
                      ELLE_TRACE("chosen block differs, signal conflict");
                      // FIXME: useless clone, find a way to steal ownership
                      throw infinit::model::doughnut::Conflict(
                        "Paxos chose a different value",
                        chosen.get()->clone());
                    }
                  }
                  else
                    break;
                }
              }
              catch (Paxos::PaxosServer::WrongQuorum const& e)
              {
                ELLE_TRACE("%s: %s instead of %s",
                           e.what(), e.effective(), e.expected());
                peers = lookup_nodes(
                  this->doughnut(), e.expected(), b->address());
                peers_id.clear();
                for (auto const& peer: peers)
                  peers_id.insert(static_cast<Peer&>(*peer).member().id());
                continue;
              }
              break;
            }
          }
          else
          {
            elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
            {
              for (auto owner: owners)
                scope.run_background(
                  "store block",
                  [&, owner] { owner->store(*b, STORE_ANY); });
              reactor::wait(scope);
            };
          }
        }

        std::unique_ptr<blocks::Block>
        Paxos::_fetch(Address address, boost::optional<int> local_version)
        {
          // FIXME: consult the quorum
          if (this->doughnut().version() >= elle::Version(0, 5, 0))
          {
            auto peers = this->_owners(address, this->_factor, overlay::OP_FETCH);
            typedef std::pair<PaxosServer::Quorum,
                      std::unique_ptr<PaxosClient::Accepted>> FetchData;
            std::vector<FetchData> hits;
            for (auto peer: peers)
            {
              try
              {
                FetchData hit;
                if (auto local = dynamic_cast<Paxos::LocalPeer*>(peer.get()))
                  hit = local->_fetch_paxos(address);
                else if (auto remote = dynamic_cast<Paxos::RemotePeer*>(peer.get()))
                  hit = remote->_fetch_paxos(address);
                else if (dynamic_cast<DummyPeer*>(peer.get()))
                  ;
                else
                  ELLE_ABORT("invalid paxos peer: %s", *peer);
                if (hit.second)
                {
                  if (!dynamic_cast<blocks::MutableBlock*>(hit.second->value.get()))
                    return hit.second->value->clone();
                  hits.push_back(std::move(hit));
                }
              }
              catch (reactor::network::Exception const& e)
              {
                ELLE_DEBUG("Network exception on %s: %s", peer, e);
              }
            }
            ELLE_TRACE("Got %s hits", hits.size());
            if (hits.empty())
              throw MissingBlock(address);
            // Reverse sort
            std::sort(hits.begin(), hits.end(),
              [] (FetchData const& a, FetchData const& b)
              {
                return a.second->proposal > b.second->proposal;
              });
            int same_quorum = 0;
            for (auto const& a: hits)
            {
              if (a.first == hits.front().first)
                ++same_quorum;
              ELLE_DEBUG("  proposal %s, quorum %s", a.second->proposal, a.first);
            }
            bool ok_size = signed(hits.size()) > signed(hits.front().first.size()) / 2;
            bool ok_same_quorum = same_quorum > signed(hits.front().first.size()) / 2;
            if ( (ok_size && ok_same_quorum) || this->_lenient_fetch)
            {
              if (auto mb = dynamic_cast<blocks::MutableBlock*>(hits.front().second->value.get()))
                if (local_version && *local_version == mb->version())
                  return std::unique_ptr<blocks::Block>();
              return hits.front().second->value->clone();
            }
            else
            {
              ELLE_TRACE("Too few peers: %s peers, %s same quorum, %s quorum size",
                         hits.size(), same_quorum, hits.front().first.size());
              throw athena::paxos::TooFewPeers(same_quorum,
                                               hits.front().first.size());
            }
          }
          else
          {
            auto peers = this->_owners(address, this->_factor, overlay::OP_FETCH);
            return fetch_from_members(peers, address, std::move(local_version));
          }
          elle::unreachable();
        }

        void
        Paxos::_remove(Address address, blocks::RemoveSignature rs)
        {
          this->remove_many(address, std::move(rs), _factor);
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
