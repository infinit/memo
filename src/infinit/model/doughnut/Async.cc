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
      Async::Async(Doughnut& doughnut)
        : Consensus(doughnut)
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
                    ConflictResolver resolver)
      {
        ELLE_TRACE("_store: %.7s", block.address());
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

        auto cpy = this->_copy(block);
        _last[cpy->address()] = cpy.get();
        _ops.put(Op{overlay,
                    op,
                    cpy->address(),
                    std::move(cpy),
                    mode});
      }

      void
      Async::_remove(overlay::Overlay& overlay,
              Address address)
      {
        ELLE_TRACE("_remove: %.7s", address);
        _ops.put({overlay, overlay::OP_REMOVE, address, nullptr, {}});
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
        return this->_owner(overlay, address, overlay::OP_FETCH)->fetch(address);
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
            overlay::Operation type = op.type;
            Address addr = op.addr;
            boost::optional<StoreMode> mode = op.mode;

            if (type == overlay::OP_REMOVE)
            {
              ELLE_TRACE("remove: %.7s", addr);
              this->_owner(overlay, addr, type)->remove(addr);
            }
            else // store
            {
              this->_owner(overlay, addr, type)->store(*(op.block), *mode);

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
