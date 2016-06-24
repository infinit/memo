#include <infinit/model/doughnut/Consensus.hh>

#include <elle/os/environ.hh>

#include <infinit/storage/MissingKey.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/MissingBlock.hh>

#include <reactor/Channel.hh>
#include <reactor/Scope.hh>
#include <reactor/scheduler.hh>
#include <reactor/network/exception.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.consensus.Consensus");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        Consensus::Consensus(Doughnut& doughnut)
          : _doughnut(doughnut)
        {}

        void
        Consensus::store(std::unique_ptr<blocks::Block> block,
                         StoreMode mode,
                         std::unique_ptr<ConflictResolver> resolver)
        {
          ELLE_TRACE_SCOPE("%s: store %s", *this, block);
          this->_store(std::move(block), mode, std::move(resolver));
        }

        void
        Consensus::_store(std::unique_ptr<blocks::Block> block,
                          StoreMode mode,
                          std::unique_ptr<ConflictResolver> resolver)
        {
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
          auto owner =  this->_owner(block->address(), op);
          std::unique_ptr<blocks::Block> nb;
          while (true)
          {
            try
            {
              if (auto o = owner.lock())
              {
                // FIXME: give ownership of block
                o->store(nb ? *nb : *block, mode);
                break;
              }
              else
                throw model::MissingBlock(block->address());
            }
            catch (Conflict const& c)
            {
              if (!resolver)
              {
                ELLE_TRACE("fatal conflict: %s", c);
                throw;
              }
              ELLE_DEBUG_SCOPE("resolve conflict: %s", c);
              nb = (*resolver)(*block, *c.current(), mode);
              if (!nb)
              {
                ELLE_TRACE("conflict resolution failed: %s", c);
                throw;
              }
              ELLE_DEBUG("seal resolved block: %s", c)
                nb->seal();
            }
          }
        }

        std::unique_ptr<blocks::Block>
        Consensus::fetch(Address address, boost::optional<int> local_version)
        {
          ELLE_TRACE_SCOPE("%s: fetch %f (local version: %s)",
                           *this, address, local_version);
          if (this->doughnut().version() < elle::Version(0, 5, 0))
            return this->_fetch(address, local_version);
          return this->_fetch(address, local_version);
        }

        void
        Consensus::fetch(std::vector<AddressVersion> const& addresses,
                         std::function<void(Address, std::unique_ptr<blocks::Block>,
                           std::exception_ptr)> res)
        {
           ELLE_TRACE_SCOPE("%s: fetch %s", *this, addresses);
           this->_fetch(addresses, res);
        }

        void
        Consensus::_fetch(std::vector<AddressVersion> const& addresses,
                         std::function<void(Address, std::unique_ptr<blocks::Block>,
                           std::exception_ptr)> res)
        {
          for (auto a: addresses)
          {
            try
            {
              auto block = this->fetch(a.first, a.second);
              res(a.first, std::move(block), {});
            }
            catch (elle::Error const& e)
            {
              res(a.first, {}, std::current_exception());
            }
          }
        }

        std::unique_ptr<blocks::Block>
        Consensus::_fetch(Address address, boost::optional<int> last_version)
        {
          if (auto owner = this->_owner(address, overlay::OP_FETCH).lock())
            return owner->fetch(address, std::move(last_version));
          else
            throw model::MissingBlock(address);
        }

        void
        Consensus::remove(Address address, blocks::RemoveSignature rs)
        {
          int count = 0;
          while (true)
          {
            ++count;
            try
            {
              this->_remove(address, std::move(rs));
              return;
            }
            catch (Conflict const& e)
            {
              if (!(count%10))
                ELLE_LOG("%s: edit conflict removing block %f, retrying",
                         this, address);
              auto block = this->fetch(address);
              rs = block->sign_remove(this->doughnut());
            }
          }
        }

        void
        Consensus::_remove(Address address, blocks::RemoveSignature rs)
        {
          if (auto owner = this->_owner(address, overlay::OP_FETCH).lock())
            owner->remove(address, std::move(rs));
          else
            throw model::MissingBlock(address);
        }

        overlay::Overlay::WeakMember
        Consensus::_owner(Address const& address,
                          overlay::Operation op) const
        {
          return this->doughnut().overlay()->lookup(address, op);
        }

        reactor::Generator<overlay::Overlay::WeakMember>
        Consensus::_owners(Address const& address,
                           int factor,
                           overlay::Operation op) const
        {
          ELLE_DEBUG_SCOPE("search %s nodes for %f", factor, address);
          return this->doughnut().overlay()->lookup(address, factor, op);
        }

        void
        Consensus::remove_many(Address address,
                               blocks::RemoveSignature rs,
                               int factor)
        {
          auto peers = this->_owners(address, factor, overlay::OP_REMOVE);
          int count = 0;
          elle::With<reactor::Scope>() <<  [&] (reactor::Scope& s)
          {
            for (auto const& p: peers)
            {
              s.run_background("remove", [this, p, address,&count, &rs]
              {
                try
                {
                  if (auto lock = p.lock())
                    lock->remove(address, rs);
                  else
                    ELLE_TRACE("peer was destroyed while removing");
                  ++count;
                }
                catch (reactor::network::Exception const& e)
                {
                  ELLE_TRACE("network exception removing %f: %s",
                             address, e.what());
                }
                catch (infinit::protocol::Serializer::EOF const&)
                {
                  ELLE_TRACE("EOF while removing %f", address);
                }
              });
            }
            reactor::wait(s);
          };
          if (!count)
            throw MissingBlock(address);
        }

        std::unique_ptr<blocks::Block>
        Consensus::fetch_from_members(
          reactor::Generator<overlay::Overlay::WeakMember>& peers,
          Address address,
          boost::optional<int> local_version)
        {
          std::unique_ptr<blocks::Block> result;
          reactor::Channel<overlay::Overlay::Member> connected;
          typedef
            reactor::Generator<overlay::Overlay::WeakMember> PeerGenerator;
          bool hit = false;
          // try connecting to all peers in parallel
          auto connected_peers = PeerGenerator(
            [&](PeerGenerator::yielder yield)
            {
              elle::With<reactor::Scope>() <<
              [&peers,&yield,&hit] (reactor::Scope& s)
              {
                for (auto wp: peers)
                {
                  auto p = wp.lock();
                  if (!p)
                    ELLE_TRACE("peer was deleted while fetching");
                  hit = true;
                  s.run_background(elle::sprintf("connect to %s", *p),
                  [p,&yield]
                  {
                    try
                    {
                      auto remote = std::dynamic_pointer_cast<Remote>(p);
                      if (remote)
                        remote->safe_perform<void>("connect", [&] { yield(p);});
                      else
                        yield(p);
                    }
                    catch (elle::Error const& e)
                    {
                      ELLE_TRACE("connect to peer %s failed: %s", p, e.what());
                    }
                  });
                }
                reactor::wait(s);
              };
          });
          // try to get on all connected peers sequentially to avoid wasting
          // bandwidth
          int attempt = 0;
          for (auto wpeer: connected_peers)
          {
            auto peer = wpeer.lock();
            if (!peer)
              ELLE_TRACE("peer was deleted while fetching");
            ++attempt;
            try
            {
              ELLE_TRACE_SCOPE("fetch from %s", peer);
              return peer->fetch(address, local_version);
            }
            // FIXME: get rid of that
            catch (elle::Error const& e)
            {
              ELLE_TRACE("attempt fetching %f from %s failed: %s",
                         address, *peer, e.what());
            }
          }
          // Some overlays may return peers even if they don't have the block,
          // so we have to return MissingBlock here.
          ELLE_TRACE("all %s peers failed fetching %f", attempt, address);
          throw MissingBlock(address);
        }

        /*-----.
        | Stat |
        `-----*/

        void
        Consensus::Stat::serialize(elle::serialization::Serializer& s)
        {}

        std::unique_ptr<Consensus::Stat>
        Consensus::stat(Address const& address)
        {
          return elle::make_unique<Stat>();
        }

        /*--------.
        | Factory |
        `--------*/

        std::unique_ptr<Local>
        Consensus::make_local(boost::optional<int> port,
                              std::unique_ptr<storage::Storage> storage)
        {
          return elle::make_unique<Local>(this->doughnut(),
                                          this->doughnut().id(),
                                          std::move(storage),
                                          port ? port.get() : 0);
        }

        /*----------.
        | Printable |
        `----------*/

        void
        Consensus::print(std::ostream& output) const
        {
          elle::fprintf(output,
                        "%f(%x)", elle::type_info(*this), (void*)(this));
        }

        /*--------------.
        | Configuration |
        `--------------*/

        Configuration::Configuration(elle::serialization::SerializerIn&)
        {}

        std::unique_ptr<Consensus>
        Configuration::make(model::doughnut::Doughnut& dht)
        {
          return elle::make_unique<Consensus>(dht);
        }

        void
        Configuration::serialize(elle::serialization::Serializer&)
        {}

        static const elle::serialization::Hierarchy<Configuration>::
        Register<Configuration> _register_Configuration("single");
      }
    }
  }
}
