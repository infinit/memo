#include <infinit/model/doughnut/Doughnut.hh>

#include <elle/Error.hh>
#include <elle/log.hh>

#include <infinit/model/doughnut/Remote.hh>


ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Doughnut::Doughnut(std::unique_ptr<overlay::Overlay> overlay)
        : _overlay(std::move(overlay))
      {}

      std::unique_ptr<blocks::Block>
      Doughnut::_make_block() const
      {
        ELLE_TRACE_SCOPE("%s: create block", *this);
        return elle::make_unique<blocks::Block>(Address::random());
      }

      void
      Doughnut::_store(blocks::Block& block)
      {
        this->_owner(block.address())->store(block);
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address) const
      {
        return this->_owner(address)->fetch(address);
      }

      void
      Doughnut::_remove(Address address)
      {
        this->_owner(address)->remove(address);
      }

      std::unique_ptr<Peer>
      Doughnut::_owner(Address const& address) const
      {
        return elle::make_unique<Remote>(this->_overlay->lookup(address));
      }
    }
  }
}
