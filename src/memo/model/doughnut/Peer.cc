#include <elle/log.hh>

#include <memo/model/doughnut/Peer.hh>
#include <memo/model/blocks/MutableBlock.hh>

ELLE_LOG_COMPONENT("memo.model.doughnut.Peer");

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      Peer::Peer(Doughnut& dht, Address id)
        : _doughnut(dht)
        , _id(std::move(id))
      {}

      Peer::~Peer()
      {}

      void
      Peer::cleanup()
      {
        ELLE_TRACE_SCOPE("%s: cleanup", this);
        this->_cleanup();
      }

      void
      Peer::_cleanup()
      {}

      /*-------.
      | Blocks |
      `-------*/

      std::unique_ptr<blocks::Block>
      Peer::fetch(Address address,
                  boost::optional<int> local_version) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %f", this, address);
        auto res = this->_fetch(std::move(address), std::move(local_version));
        if (local_version)
          if (auto mb = dynamic_cast<blocks::MutableBlock*>(res.get()))
            if (mb->version() == local_version.get())
              return nullptr;
        return res;
      }

      /*-----.
      | Keys |
      `-----*/

      elle::cryptography::rsa::PublicKey
      Peer::resolve_key(int id)
      {
        return this->resolve_keys({id})[0];
      }

      std::vector<elle::cryptography::rsa::PublicKey>
      Peer::resolve_keys(std::vector<int> const& ids)
      {
        return this->_resolve_keys(ids);
      }

      std::unordered_map<int, elle::cryptography::rsa::PublicKey>
      Peer::resolve_all_keys()
      {
        return this->_resolve_all_keys();
      }


      /*----------.
      | Printable |
      `----------*/

      void
      Peer::print(std::ostream& stream) const
      {
        elle::fprintf(stream, "%f(%x, %f)",
                      elle::type_info(*this),
                      reinterpret_cast<void const*>(this),
                      this->id());
      }
    }
  }
}
