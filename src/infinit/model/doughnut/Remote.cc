#include <infinit/model/doughnut/Remote.hh>

#include <elle/log.hh>

#include <infinit/RPC.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Remote")

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      Remote::Remote(std::string const& host, int port)
        : _socket(host, port)
        , _serializer(this->_socket)
        , _channels(this->_serializer)
      {}

      /*-------.
      | Blocks |
      `-------*/

      void
      Remote::store(blocks::Block& block)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        RPC<void (Address, elle::Buffer&)> store("store", this->_channels);
        store(block.address(), block.data());
      }

      std::unique_ptr<blocks::Block>
      Remote::fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
        RPC<elle::Buffer (Address)> fetch("fetch",
                                          const_cast<Remote*>(this)->_channels);
        return elle::make_unique<blocks::Block>(address, fetch(address));
      }

      void
      Remote::remove(Address address)
      {
        ELLE_TRACE_SCOPE("%s: remove %x", *this, address);
        RPC<void (Address)> remove("remove", this->_channels);
        remove(address);
      }
    }
  }
}
