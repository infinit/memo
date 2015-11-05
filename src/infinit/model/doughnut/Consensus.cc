#include <infinit/model/doughnut/Consensus.hh>

#include <elle/os/environ.hh>

#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/Conflict.hh>
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
        Consensus::store(overlay::Overlay& overlay,
                         std::unique_ptr<blocks::Block> block,
                         StoreMode mode,
                         std::unique_ptr<ConflictResolver> resolver)
        {
          ELLE_TRACE_SCOPE("%s: store %s", *this, block);
          this->_store(overlay, std::move(block), mode, std::move(resolver));
        }

        void
        Consensus::_store(overlay::Overlay& overlay,
                         std::unique_ptr<blocks::Block> block,
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
          auto owner =  this->_owner(overlay, block->address(), op);
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
              nb = (*resolver)(*block, mode);
              if (!nb)
                throw;
              nb->seal();
            }
          }
        }

        std::unique_ptr<blocks::Block>
        Consensus::fetch(overlay::Overlay& overlay, Address address)
        {
          return this->_fetch(overlay, address);
        }

        std::unique_ptr<blocks::Block>
        Consensus::_fetch(overlay::Overlay& overlay, Address address)
        {
          return
            this->_owner(overlay, address, overlay::OP_FETCH)->fetch(address);
        }

        void
        Consensus::remove(overlay::Overlay& overlay, Address address)
        {
          return this->_remove(overlay, address);
        }

        void
        Consensus::_remove(overlay::Overlay& overlay, Address address)
        {
          this->_owner(overlay, address, overlay::OP_REMOVE)->remove(address);
        }

        std::shared_ptr<Peer>
        Consensus::_owner(overlay::Overlay& overlay,
                          Address const& address,
                          overlay::Operation op) const
        {
          return overlay.lookup(address, op);
        }

        void
        Consensus::remove_many(overlay::Overlay& overlay,
                               Address address,
                               int factor)
        {
          auto peers = overlay.lookup(address, factor, overlay::OP_REMOVE);
          int count = 0;
          elle::With<reactor::Scope>() <<  [&] (reactor::Scope& s)
          {
            for (auto const& p: peers)
            {
              s.run_background("remove", [this, p, address,&count]
              {
                for (int i=0; i<5; ++i)
                {
                  try
                  {
                    if (i!=0)
                      p->reconnect();
                    p->remove(address);
                    ++count;
                    return;
                  }
                  catch (reactor::network::Exception const& e)
                  {
                    ELLE_TRACE("%s: network exception %s", *this, e);
                    reactor::sleep(
                      boost::posix_time::milliseconds(20 * pow(2, i)));
                  }
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
          reactor::Generator<overlay::Overlay::Member>& peers, Address address)
        {
          static const int timeout_sec =
            std::stoi(elle::os::getenv("INFINIT_CONNECT_TIMEOUT", "0"));
          elle::DurationOpt timeout;
          if (timeout_sec)
            timeout = boost::posix_time::seconds(timeout_sec);
          std::unique_ptr<blocks::Block> result;
          reactor::Channel<overlay::Overlay::Member> connected;
          typedef reactor::Generator<overlay::Overlay::Member> PeerGenerator;
          bool hit = false;
          // try connecting to all peers in parallel
          auto connected_peers = PeerGenerator(
            [&](PeerGenerator::yielder yield)
            {
              elle::With<reactor::Scope>() <<
              [&peers,&yield,&hit,&timeout] (reactor::Scope& s)
              {
                for (auto p: peers)
                {
                  hit = true;
                  s.run_background(elle::sprintf("connect to %s", *p),
                  [p,&yield,&timeout]
                  {
                    for (int i=0; i<5; ++i)
                    {
                      ELLE_DEBUG_SCOPE("connect to %s", *p);
                      try
                      {
                        if (i!=0)
                          p->reconnect(timeout);
                        else
                          p->connect(timeout);
                        yield(p);
                        return;
                      }
                      catch (reactor::network::Exception const& e)
                      {
                        ELLE_TRACE("network exception %s", e);
                        reactor::sleep(
                          boost::posix_time::milliseconds(20 * pow(2, i)));
                      }
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
              return peer->fetch(address);
            }
            catch (elle::Error const& e)
            {
              ELLE_TRACE("attempt fetching %s from %s failed: %s",
                         address, *peer, e.what());
            }
          }
          // Some overlays may return peers even if they don't have the block,
          // so we have to return MissingBlock here.
          throw MissingBlock(address);
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
