#include <infinit/model/doughnut/Consensus.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/Conflict.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Consensus::Consensus(Doughnut& doughnut)
        : _doughnut(doughnut)
      {}

      void
      Consensus::store(overlay::Overlay& overlay,
                       blocks::Block& block,
                       StoreMode mode,
                       std::unique_ptr<ConflictResolver> resolver)
      {
        this->_store(overlay, block, mode, std::move(resolver));
      }

      void
      Consensus::_store(overlay::Overlay& overlay,
                       blocks::Block& block,
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
        auto owner =  this->_owner(overlay, block.address(), op);
        std::unique_ptr<blocks::Block> nb;
        while (true)
        {
          try
          {
            owner->store(nb? *nb : block, mode);
            (nb? nb.get() : &block)->stored();
            break;
          }
          catch (Conflict const& c)
          {
            if (!resolver)
              throw;
            nb = (*resolver)(block, mode);
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
    }
  }
}
