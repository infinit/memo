#include <infinit/model/doughnut/Async.hh>
#include <elle/serialization/json.hh>

#include <reactor/exception.hh>
#include <reactor/scheduler.hh>
#include <infinit/model/MissingBlock.hh>

#include <iostream>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Async");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Async::Async(Doughnut& doughnut,
                   std::unique_ptr<Consensus> backend)
        : Consensus(doughnut)
        , _backend(std::move(backend))
        , _process_thread("async consensus", [&] { _process_loop();})
      {}

      Async::~Async()
      {
        ELLE_TRACE("~Async");
        _process_thread.terminate_now();
        ELLE_TRACE("~~Async");
      }

      void
      Async::_store(overlay::Overlay& overlay,
                    blocks::Block& block,
                    StoreMode mode,
                    std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE("_store: %.7s", block.address());

        auto cpy = this->_copy(block);
        _last[cpy->address()] = cpy.get();
        _ops.put(Op{overlay,
                    cpy->address(),
                    std::move(cpy),
                    mode,
                    std::move(resolver)
        });
      }

      void
      Async::_remove(overlay::Overlay& overlay,
              Address address)
      {
        ELLE_TRACE("_remove: %.7s", address);
        _ops.put({overlay, address, nullptr, {}});
      }

      // Fetch operation must be synchronious, else the consistency is not
      // preserved.
      std::unique_ptr<blocks::Block>
      Async::_fetch(overlay::Overlay& overlay,
                    Address address)
      {
        ELLE_TRACE("_fetch: %.7s", address);
        if (_last.find(address) != _last.end())
        {
          ELLE_DUMP("_fetch: cache");
          auto cpy = this->_copy(*(_last[address]));
          ELLE_DUMP("_fetch: cpy'd block data(%.7s): %s", cpy->address(), cpy->data());
          return cpy;
        }

        ELLE_DUMP("_fetch: network");
        return this->_backend->fetch(overlay, address);
      }

      void
      Async::_process_loop()
      {
        while (true)
        {
          try
          {
            Op op = _ops.get();

            overlay::Overlay& overlay = op.overlay;
            Address addr = op.addr;
            boost::optional<StoreMode> mode = op.mode;
            std::unique_ptr<ConflictResolver>& resolver = op.resolver;

            if (!mode)
            {
              ELLE_TRACE("remove: %.7s", addr);
              this->_backend->remove(overlay, addr);
            }
            else // store
            {
              this->_backend->store(overlay, *op.block, *mode, std::move(resolver));
              if (op.block.get() == _last[addr])
              {
                ELLE_DUMP("store: block(%.7s) data: %s", addr, _last[addr]->data());
                _last.erase(addr);
                ELLE_DUMP("store: %.7s removed from cache", addr);
                for (auto const& i: _last)
                  ELLE_DUMP("store: _last[%.7s] = %p", i.first, i.second);
              }

              ELLE_TRACE("store: %.7s OK", addr);
            }
          }
          catch (reactor::Terminate const&)
          {
            ELLE_TRACE("Terminating thread");
            throw;
          }
          catch (std::exception const& e)
          {
            ELLE_WARN("Exception escaped Async loop: %s", e.what());
          }
        }
      }

      std::unique_ptr<blocks::Block>
      Async::_copy(blocks::Block& block) const
      {
        std::stringstream ss;
        elle::serialization::json::serialize(block, ss, false);
        elle::serialization::json::SerializerIn out(ss, false);
        out.set_context<Doughnut*>(&this->_doughnut);
        return out.deserialize<std::unique_ptr<blocks::Block>>();
      }
    } // namespace doughnut
  } // namespace model
} // namespace infinit
