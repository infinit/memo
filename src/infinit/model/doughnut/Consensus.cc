#include <infinit/model/doughnut/Consensus.hh>

#include <elle/os/environ.hh>

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
          auto owner =  this->_owner(block->address(), op);
          std::unique_ptr<blocks::Block> nb;
          while (true)
          {
            try
            {
              owner->store(nb ? *nb : *block, mode);
              break;
            }
            catch (Conflict const& c)
            {
              if (!resolver)
                throw;
              ELLE_ABORT("FIXME: pass new block to base Consensus "
                         "conflict resolution");
              // nb = (*resolver)(*block, mode);
              // if (!nb)
              //   throw;
              // nb->seal();
            }
          }
        }

        std::unique_ptr<blocks::Block>
        Consensus::fetch(Address address, boost::optional<int> local_version)
        {
          ELLE_TRACE_SCOPE("%s: fetch %s (local version: %s)",
                           *this, address, local_version);
          return this->_fetch(address, std::move(local_version));
        }

        std::unique_ptr<blocks::Block>
        Consensus::_fetch(Address address, boost::optional<int> last_version)
        {
          return this->_owner(address, overlay::OP_FETCH)->fetch(
            address, std::move(last_version));
        }

        void
        Consensus::remove(Address address)
        {
          return this->_remove(address);
        }

        void
        Consensus::_remove(Address address)
        {
          this->_owner(address, overlay::OP_REMOVE)->remove(address);
        }

        std::shared_ptr<Peer>
        Consensus::_owner(Address const& address,
                          overlay::Operation op) const
        {
          return this->doughnut().overlay()->lookup(address, op);
        }

        reactor::Generator<overlay::Overlay::Member>
        Consensus::_owners(Address const& address,
                           int factor,
                           overlay::Operation op) const
        {
          return this->doughnut().overlay()->lookup(address, factor, op);
        }

        void
        Consensus::remove_many(Address address, int factor)
        {
          auto peers = this->_owners(address, factor, overlay::OP_REMOVE);
          int count = 0;
          elle::With<reactor::Scope>() <<  [&] (reactor::Scope& s)
          {
            for (auto const& p: peers)
            {
              s.run_background("remove", [this, p, address,&count]
              {
                p->remove(address);
                ++count;
              });
            }
            reactor::wait(s);
          };
          if (!count)
            throw MissingBlock(address);
        }

        std::unique_ptr<blocks::Block>
        Consensus::fetch_from_members(
          reactor::Generator<overlay::Overlay::Member>& peers,
          Address address,
          boost::optional<int> local_version)
        {
          std::unique_ptr<blocks::Block> result;
          reactor::Channel<overlay::Overlay::Member> connected;
          typedef reactor::Generator<overlay::Overlay::Member> PeerGenerator;
          bool hit = false;
          // try connecting to all peers in parallel
          auto connected_peers = PeerGenerator(
            [&](PeerGenerator::yielder yield)
            {
              elle::With<reactor::Scope>() <<
              [&peers,&yield,&hit] (reactor::Scope& s)
              {
                for (auto p: peers)
                {
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
          for (auto peer: connected_peers)
          {
            try
            {
              ELLE_TRACE_SCOPE("fetch from %s", *peer);
              return peer->fetch(address, local_version);
            }
            catch (elle::Error const& e)
            {
              ELLE_TRACE("attempt fetching %s from %s failed: %s",
                         address, *peer, e.what());
            }
          }
          // Some overlays may return peers even if they don't have the block,
          // so we have to return MissingBlock here.
          ELLE_TRACE("Peers exhausted, throwing missingblock(%x)", address);
          throw MissingBlock(address);
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
          elle::fprintf(output, "%s(%x)", elle::type_info(*this), this);
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
