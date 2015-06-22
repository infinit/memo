#include <infinit/model/doughnut/Doughnut.hh>

#include <elle/Buffer.hh>
#include <elle/Error.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh> // FIXME

#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/Remote.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

#include <infinit/model/doughnut/ACB.cc>
#include <infinit/model/doughnut/CHB.cc>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Doughnut::Doughnut(cryptography::KeyPair keys,
                         std::unique_ptr<overlay::Overlay> overlay)
        : _overlay(std::move(overlay))
        , _keys(keys)
      {}

      std::unique_ptr<blocks::MutableBlock>
      Doughnut::_make_mutable_block() const
      {
        ELLE_TRACE_SCOPE("%s: create OKB", *this);
        return elle::make_unique<OKB>(this->_keys);
      }

      std::unique_ptr<blocks::ImmutableBlock>
      Doughnut::_make_immutable_block(elle::Buffer content) const
      {
        ELLE_TRACE_SCOPE("%s: create CHB", *this);
        return elle::make_unique<CHB>(std::move(content));
      }

      void
      Doughnut::_store(blocks::Block& block, StoreMode mode)
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
        }
        this->_owner(block.address(), op)->store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address) const
      {
        return this->_owner(address, overlay::OP_FETCH)->fetch(address);
      }

      void
      Doughnut::_remove(Address address)
      {
        this->_owner(address, overlay::OP_REMOVE)->remove(address);
      }

      std::unique_ptr<Peer>
      Doughnut::_owner(Address const& address, overlay::Operation op) const
      {
        return elle::make_unique<Remote>(this->_overlay->lookup(address, op));
      }
    }
  }
}
