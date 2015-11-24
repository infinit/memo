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
      Peer::Peer(Address id)
        : _id(std::move(id))
      {}

      Peer::~Peer()
      {}

      std::unique_ptr<blocks::Block>
      Peer::fetch(Address address,
                  boost::optional<int> local_version) const
      {
        ELLE_TRACE_SCOPE("%s: fetch %x", *this, address);
        auto res = this->_fetch(std::move(address), std::move(local_version));
        if (local_version)
          if (auto mb = dynamic_cast<blocks::MutableBlock*>(res.get()))
            if (mb->version() == local_version.get())
              return nullptr;
        return res;
      }
    }
  }
}
