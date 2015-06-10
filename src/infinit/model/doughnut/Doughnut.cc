#include <infinit/model/doughnut/Doughnut.hh>

#include <elle/Error.hh>
#include <elle/log.hh>


ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Doughnut::Doughnut(std::vector<std::unique_ptr<Peer>> peers)
        : _peers(std::move(peers))
      {
        if (this->_peers.empty())
          throw elle::Error("empty peer list");
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_make_block() const
      {
        ELLE_TRACE_SCOPE("%s: create block", *this);
        return elle::make_unique<blocks::Block>(Address::random());
      }

      void
      Doughnut::_store(blocks::Block& block)
      {
        this->_owner(block.address()).store(block);
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address) const
      {
        return this->_owner(address).fetch(address);
      }

      void
      Doughnut::_remove(Address address)
      {
        this->_owner(address).remove(address);
      }

      Peer&
      Doughnut::_owner(Address const& address) const
      {
        auto idx = address.value()[0] % this->_peers.size(); // FIXME
        return *this->_peers[idx];
      }
    }
  }
}
