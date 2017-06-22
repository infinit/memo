#include <infinit/model/doughnut/Consensus.hh>

#include <elle/os/environ.hh>

#include <infinit/silo/MissingKey.hh>
#include <infinit/model/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/MissingBlock.hh>

#include <elle/reactor/Channel.hh>
#include <elle/reactor/Scope.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/network/Error.hh>

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
          auto owner = [&]
          {
            switch (mode)
            {
              case STORE_INSERT:
                for (auto owner: this->doughnut().overlay()->allocate(block->address(), 1))
                  return owner;
              case STORE_UPDATE:
                return this->doughnut().overlay()->lookup(block->address());
            }
            ELLE_ABORT("unrecognized store mode: %s", mode);
          }();
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
              nb = (*resolver)(*block, *c.current());
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
          ELLE_TRACE_SCOPE("%s: fetch %f if newer than %s",
                           *this, address, local_version);
          return this->_fetch(address, local_version);
        }

        void
        Consensus::fetch(std::vector<AddressVersion> const& addresses,
                         ReceiveBlock res)
        {
           ELLE_TRACE_SCOPE("%s: fetch %s", *this, addresses);
           this->_fetch(addresses, res);
        }

        void
        Consensus::_fetch(std::vector<AddressVersion> const& addresses,
                          ReceiveBlock res)
        {
          for (auto const& a: addresses)
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
          if (auto owner = this->doughnut().overlay()->lookup(address).lock())
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
          if (auto owner = this->doughnut().overlay()->lookup(address).lock())
            owner->remove(address, std::move(rs));
          else
            throw model::MissingBlock(address);
        }

        void
        Consensus::resign()
        {
          ELLE_TRACE_SCOPE("%s: resign", this);
          this->_resign();
        }

        void
        Consensus::_resign()
        {}

        void
        Consensus::remove_many(Address address,
                               blocks::RemoveSignature rs,
                               int factor)
        {
          // NonInterruptible blocks ensure we don't stack exceptions in Remote
          // destructor.
          auto peers = this->doughnut().overlay()->lookup(address, factor);
          int count = 0;
          elle::With<elle::reactor::Scope>() <<  [&] (elle::reactor::Scope& s)
          {
            for (auto p: peers)
            {
              s.run_background("remove", [p, address, &count, &rs]
              {
                if (auto lock = p.lock())
                {
                  auto const cleanup = [&]
                    {
                      elle::With<elle::reactor::Thread::NonInterruptible>() << [&]
                      {
                        lock.reset();
                        elle::unconst(p).reset();
                      };
                    };
                  try
                  {
                    lock->remove(address, rs);
                    cleanup();
                  }
                  catch (elle::reactor::network::Error const& e)
                  {
                    ELLE_TRACE("network exception removing %f: %s",
                               address, e.what());
                    cleanup();
                  }
                  catch (elle::protocol::Serializer::EOF const&)
                  {
                    ELLE_TRACE("EOF while removing %f", address);
                    cleanup();
                  }
                  catch (...)
                  {
                    cleanup();
                    throw;
                  }
                  ++count;
                }
                else
                {
                  ELLE_TRACE("peer was destroyed while removing");
                  elle::With<elle::reactor::Thread::NonInterruptible>() << [&]
                  {
                    elle::unconst(p).reset();
                  };
                }
              });
            }
            elle::reactor::wait(s);
          };
          if (!count)
            throw MissingBlock(address);
        }

        std::unique_ptr<blocks::Block>
        Consensus::fetch_from_members(MemberGenerator& peers,
                                      Address address,
                                      boost::optional<int> local_version)
        {
          bool hit = false;
          // try connecting to all peers in parallel
          auto connected_peers = MemberGenerator(
            [&](MemberGenerator::yielder yield)
            {
              elle::With<elle::reactor::Scope>() <<
              [&peers,&yield,&hit] (elle::reactor::Scope& s)
              {
                for (auto wp: peers)
                  if (auto p = wp.lock())
                  {
                    hit = true;
                    s.run_background(elle::sprintf("connect to %s", *p),
                    [p,&yield]
                    {
                      try
                      {
                        if (auto remote = std::dynamic_pointer_cast<Remote>(p))
                          remote->safe_perform("connect", [&] { yield(p);});
                        else
                          yield(p);
                      }
                      catch (elle::Error const& e)
                      {
                        ELLE_TRACE("connect to peer %s failed: %s", p, e.what());
                      }
                    });
                  }
                  else
                    ELLE_TRACE("peer was deleted while fetching");

                elle::reactor::wait(s);
              };
          });
          // try to get on all connected peers sequentially to avoid wasting
          // bandwidth
          int attempt = 0;
          for (auto wpeer: connected_peers)
            if (auto peer = wpeer.lock())
            {
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
            else
              ELLE_TRACE("peer was deleted while fetching");
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
          return std::make_unique<Stat>();
        }

        /*--------.
        | Factory |
        `--------*/

        std::unique_ptr<Local>
        Consensus::make_local(
          boost::optional<int> port,
          boost::optional<boost::asio::ip::address> listen_address,
          std::unique_ptr<silo::Silo> storage)
        {
          return std::make_unique<Local>(this->doughnut(),
                                         this->doughnut().id(),
                                         std::move(storage),
                                         port.value_or(0),
                                         listen_address);
        }

        std::shared_ptr<Remote>
        Consensus::make_remote(std::shared_ptr<Dock::Connection> connection)
        {
          return std::make_shared<Remote>(this->doughnut(),
                                          std::move(connection));
        }

        /*-----------.
        | Monitoring |
        `-----------*/

        elle::json::Object
        Consensus::redundancy()
        {
          return {
            { "desired_factor", 1.0 },
            { "type", "none" },
          };
        }

        elle::json::Object
        Consensus::stats()
        {
          return {
            { "type", "none" },
          };
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

        /*-----------------.
        | StackedConsensus |
        `-----------------*/

        StackedConsensus::StackedConsensus(std::unique_ptr<Consensus> backend)
          : Consensus(backend->doughnut())
          , _backend(std::move(backend))
        {}

        std::shared_ptr<Remote>
        StackedConsensus::make_remote(std::shared_ptr<Dock::Connection> c)
        {
          return this->_backend->make_remote(std::move(c));
        }

        /*--------------.
        | Configuration |
        `--------------*/

        Configuration::Configuration(elle::serialization::SerializerIn&)
        {}

        std::unique_ptr<Configuration>
        Configuration::clone() const
        {
          return std::unique_ptr<Configuration>(new Configuration());
        }

        std::unique_ptr<Consensus>
        Configuration::make(model::doughnut::Doughnut& dht)
        {
          return std::make_unique<Consensus>(dht);
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
