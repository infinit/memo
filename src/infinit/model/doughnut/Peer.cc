#include <elle/log.hh>

#include <infinit/model/doughnut/Peer.hh>
#include <infinit/model/blocks/MutableBlock.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Peer");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Peer::Peer(Doughnut& dht, Address id)
        : _doughnut(dht)
        , _id(std::move(id))
      {}

      Peer::~Peer()
      {}

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

      cryptography::rsa::PublicKey
      Peer::resolve_key(int id)
      {
        return this->resolve_keys({id})[0];
      }

      std::vector<cryptography::rsa::PublicKey>
      Peer::resolve_keys(std::vector<int> const& ids)
      {
        return this->_resolve_keys(ids);
      }

      std::unordered_map<int, cryptography::rsa::PublicKey>
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
        elle::fprintf(stream, "%f(%f)", elle::type_info(*this), this->id());
      }
    }
  }
}
