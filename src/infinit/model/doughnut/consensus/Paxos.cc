#include <infinit/model/doughnut/consensus/Paxos.hh>

#include <functional>

#include <elle/memory.hh>

#include <cryptography/rsa/PublicKey.hh>
#include <cryptography/hash.hh>

#include <infinit/RPC.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Paxos");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
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

        Paxos::Paxos(Doughnut& doughnut, int factor)
          : Super(doughnut)
          , _factor(factor)
        {}

        /*-----.
        | Peer |
        `-----*/

        class Peer
          : public Paxos::PaxosClient::Peer
        {
        public:
          Peer(reactor::Generator<overlay::Overlay::Member>& members,
               std::vector<Address> peers,
               Address address,
               int version)
            : _members(&members)
            , _member()
            , _peers(std::move(peers))
            , _address(address)
            , _version(version)
          {}

          Peer(overlay::Overlay::Member member,
               std::vector<Address> peers,
               Address address,
               int version)
            : _members(nullptr)
            , _member(std::move(member))
            , _peers(std::move(peers))
            , _address(address)
            , _version(version)
          {}

          virtual
          boost::optional<Paxos::PaxosClient::Accepted>
          propose(Paxos::PaxosClient::Proposal const& p)
          {
            auto& member = this->member();
            member.connect();
            if (auto local = dynamic_cast<Paxos::LocalPeer*>(&member))
              return local->propose(
                this->_peers, this->_address, this->_version, p);
            else if (auto remote = dynamic_cast<Paxos::RemotePeer*>(&member))
              return remote->propose(
                this->_peers, this->_address, this->_version, p);
            ELLE_ABORT("invalid paxos peer: %s", member);
          }

          virtual
          Paxos::PaxosClient::Proposal
          accept(Paxos::PaxosClient::Proposal const& p,
                 std::shared_ptr<blocks::Block> const& value)
          {
            auto& member = this->member();
            member.connect();
            if (auto local = dynamic_cast<Paxos::LocalPeer*>(&member))
              return local->accept(
                this->_peers, this->_address, this->_version, p, value);
            else if (auto remote = dynamic_cast<Paxos::RemotePeer*>(&member))
              return remote->accept(
                this->_peers, this->_address, this->_version, p, value);
            ELLE_ABORT("invalid paxos peer: %s", member);
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
          ELLE_ATTRIBUTE(std::vector<Address>, peers);
          ELLE_ATTRIBUTE(Address, address);
          ELLE_ATTRIBUTE(int, version);
        };

        /*-----------.
        | RemotePeer |
        `-----------*/

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::RemotePeer::propose(std::vector<Address> const& peers,
                                   Address address,
                                   int version,
                                   PaxosClient::Proposal const& p)
        {
          this->connect();
          RPC<boost::optional<PaxosClient::Accepted>(
            std::vector<Address>,
            Address,
            int,
            PaxosClient::Proposal const&)>
            propose("propose", *this->_channels);
          propose.set_context<Doughnut*>(&this->_doughnut);
          return propose(peers, address, version, p);
        }

        Paxos::PaxosClient::Proposal
        Paxos::RemotePeer::accept(std::vector<Address> const& peers,
                                  Address address,
                                  int version,
                                  Paxos::PaxosClient::Proposal const& p,
                                  std::shared_ptr<blocks::Block> const& value)
        {
          this->connect();
          RPC<Paxos::PaxosClient::Proposal (
            std::vector<Address> peers,
            Address,
            int,
            Paxos::PaxosClient::Proposal const&,
            std::shared_ptr<blocks::Block> const&)>
            accept("accept", *this->_channels);
          accept.set_context<Doughnut*>(&this->_doughnut);
          return accept(peers, address, version, p, value);
        }

        /*----------.
        | LocalPeer |
        `----------*/

        boost::optional<Paxos::PaxosClient::Accepted>
        Paxos::LocalPeer::propose(std::vector<Address> peers,
                                  Address address,
                                  int version,
                                  Paxos::PaxosClient::Proposal const& p)
        {
          auto decision = this->_addresses.find(address);
          if (decision == this->_addresses.end())
            try
            {
              auto buffer = this->storage()->get(address);
              elle::serialization::Context context;
              context.set<Doughnut*>(this->doughnut());
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
                Decision(PaxosServer(uid(this->doughnut()->keys().K()),
                                     std::move(peers)))).first;
            }
          auto res = decision->second.paxos.propose(p);
          this->storage()->set(
            address,
            elle::serialization::binary::serialize(
              BlockOrPaxos(&decision->second)),
            true, true);
          return res;
        }

        Paxos::PaxosClient::Proposal
        Paxos::LocalPeer::accept(std::vector<Address> peers,
                                 Address address,
                                 int version,
                                 Paxos::PaxosClient::Proposal const& p,
                                 std::shared_ptr<blocks::Block> const& value)
        {
          ELLE_TRACE_SCOPE("%s: accept version %s of %s: %s",
                           *this, version, address, p);
          // FIXME: factor with validate in doughnut::Local::store
          ELLE_DEBUG("validate block")
            if (auto res = value->validate()); else
              throw ValidationFailed(res.reason());
          auto& decision = this->_addresses.at(address);
          auto& paxos = decision.paxos;
          auto res = paxos.accept(p, value);
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
              std::vector<Address>, Address,
              int, Paxos::PaxosClient::Proposal const&)>
            (std::bind(&LocalPeer::propose,
                       this, ph::_1, ph::_2, ph::_3, ph::_4)));
          rpcs.add(
            "accept",
            std::function<
            Paxos::PaxosClient::Proposal(
              std::vector<Address>, Address,
              int, Paxos::PaxosClient::Proposal const& p,
              std::shared_ptr<blocks::Block> const& value)>
            (std::bind(&LocalPeer::accept,
                       this, ph::_1, ph::_2, ph::_3, ph::_4, ph::_5)));
        }

        template <typename T>
        T&
        unconst(T const& v)
        {
          return const_cast<T&>(v);
        }

        std::unique_ptr<blocks::Block>
        Paxos::LocalPeer::fetch(Address address) const
        {
          ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
          auto decision = this->_addresses.find(address);
          if (decision == this->_addresses.end())
            try
            {
              elle::serialization::Context context;
              context.set<Doughnut*>(this->doughnut());
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
                "finalize running Paxos for version %s", version);
              // FIXME: use ids
              auto owners = this->doughnut()->overlay()->lookup(
                address, this->_factor, overlay::OP_UPDATE);
              // auto owners = this->doughnut()->overlay()->lookup(
              //   paxos.peers(), overlay::OP_UPDATE);
              // FIXME: factor with RemotePeer paxos client routine
              Paxos::PaxosClient::Peers peers;
              auto block = highest->value;
              for (int i = 0; i < this->_factor; ++i)
              {
                std::unique_ptr<consensus::Peer> peer(
                  new consensus::Peer(owners, paxos.peers(),
                                      block->address(), version));
                peers.push_back(std::move(peer));
              }
              Paxos::PaxosClient client(uid(this->doughnut()->keys().K()),
                                        std::move(peers));
              auto chosen = client.choose(version, block);
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
          elle::Buffer data =
            elle::serialization::binary::serialize(
              BlockOrPaxos(const_cast<blocks::Block*>(&block)));
          this->storage()->set(block.address(), data,
                              mode == STORE_ANY || mode == STORE_INSERT,
                              mode == STORE_ANY || mode == STORE_UPDATE);
          on_store(block, mode);
        }

        void
        Paxos::_store(overlay::Overlay& overlay,
                      std::unique_ptr<blocks::Block> inblock,
                      StoreMode mode,
                      std::unique_ptr<ConflictResolver> resolver)
        {
          std::shared_ptr<blocks::Block> b(inblock.release());
          do
          {
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
            auto owners = this->_owners(overlay, b->address(), op);
            if (auto* m = dynamic_cast<blocks::MutableBlock*>(b.get()))
            {
              auto version = m->version();
              // FIXME: this voids the whole "query on the fly" optimisation
              std::vector<Address> peers_id;
              std::vector<overlay::Overlay::Member> members;
              for (auto member: owners)
              {
                peers_id.push_back(member->id());
                members.push_back(member);
              }
              Paxos::PaxosClient::Peers peers;
              for (auto member: members)
              {
                peers.push_back(
                  elle::make_unique<Peer>(std::move(member), peers_id,
                                          b->address(), version));
              }
              // for (int i = 0; i < this->_factor; ++i)
              //   peers.push_back(
              //     elle::make_unique<Peer>(owners, block->address(), version));
              Paxos::PaxosClient client(uid(this->_doughnut.keys().K()),
                                        std::move(peers));
              auto chosen = client.choose(version, b);
              if (chosen && *chosen.get() != *b)
              {
                if (resolver)
                {
                  ELLE_TRACE(
                    "%s: chosen block differs, run conflict resolution", *this);
                  auto block = (*resolver)(*b, mode);
                  if (block)
                  {
                    block->seal();
                    b.reset(block.release());
                    continue;
                  }
                }
                ELLE_TRACE("%s: chosen block differs, signal conflict", *this);
                throw infinit::model::doughnut::Conflict(
                  "Paxos chose a different value");
              }
            }
            else
            {
              elle::With<reactor::Scope>() << [&] (reactor::Scope& scope)
              {
                for (auto owner: owners)
                  scope.run_background(
                    "store block",
                    [&, owner] { owner->store(*b, mode); });
                reactor::wait(scope);
              };
            }
            break;
          } while (true);
        }

        std::unique_ptr<blocks::Block>
        Paxos::_fetch(overlay::Overlay& overlay, Address address)
        {
          return
            this->_owner(overlay, address, overlay::OP_FETCH)->fetch(address);
        }

        void
        Paxos::_remove(overlay::Overlay& overlay, Address address)
        {
          this->_owner(overlay, address, overlay::OP_REMOVE)->remove(address);
        }

        reactor::Generator<overlay::Overlay::Member>
        Paxos::_owners(overlay::Overlay& overlay,
                       Address const& address,
                       overlay::Operation op) const
        {
          return overlay.lookup(address, this->_factor, op);
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
      }
    }
  }
}
