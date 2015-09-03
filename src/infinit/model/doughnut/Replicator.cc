#include <infinit/model/doughnut/Replicator.hh>
#include <infinit/model/doughnut/Remote.hh>

#include <reactor/exception.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Replicator");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Replicator::Replicator(Doughnut& doughnut, int factor)
      : Consensus(doughnut)
      , _factor(factor)
      {
      }

      void
      Replicator::_store(overlay::Overlay& overlay, blocks::Block& block, StoreMode mode)
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
        auto peers = overlay.lookup(block.address(), _factor, op, true);
        for (auto const& p: peers)
          p->store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Replicator::_fetch(overlay::Overlay& overlay, Address address)
      {
        auto peers = overlay.lookup(address, _factor, overlay::OP_FETCH, false);
        for (auto& p: peers)
        {
          try
          {
            return p->fetch(address);
          }
          catch(reactor::Terminate const& e)
          {
            throw;
          }
          catch(std::exception const& e)
          {
            ELLE_WARN("Replicator: candidate failed with %s", e);
          }
        }
        throw elle::Error("Replicator: All candidates failed.");
      }
      void
      Replicator::_remove(overlay::Overlay& overlay, Address address)
      {
        auto peers = overlay.lookup(address, _factor, overlay::OP_REMOVE, true);
        for (auto const& p: peers)
          p->remove(address);
      }
    }
  }
}