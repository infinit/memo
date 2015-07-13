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

      Remote::Remote(Doughnut& doughnut, std::string const& host, int port)
        : _doughnut(doughnut)
        , _socket(host, port)
        , _serializer(this->_socket)
        , _channels(this->_serializer)
      {}

      Remote::Remote(Doughnut& doughnut,
                     boost::asio::ip::tcp::endpoint endpoint)
        : _doughnut(doughnut)
        , _socket(std::move(endpoint))
        , _serializer(this->_socket)
        , _channels(this->_serializer)
      {}

      /*-------.
      | Blocks |
      `-------*/

      void
      Remote::store(blocks::Block const& block, StoreMode mode)
      {
        ELLE_TRACE_SCOPE("%s: store %f", *this, block);
        RPC<void (blocks::Block const&, StoreMode)> store("store", this->_channels);
        store(block, mode);
      }

      std::unique_ptr<blocks::Block>
      Remote::fetch(Address address) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
        RPC<std::unique_ptr<blocks::Block> (Address)> fetch(
          "fetch", const_cast<Remote*>(this)->_channels);
        fetch.set_context<Doughnut*>(&this->_doughnut);
        return fetch(address);
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
