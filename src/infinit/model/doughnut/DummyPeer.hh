#ifndef INFINIT_MODEL_DOUGHNUT_DUMMY_PEER_HH
# define INFINIT_MODEL_DOUGHNUT_DUMMY_PEER_HH

#include <reactor/network/exception.hh>

#include <infinit/model/doughnut/Peer.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class DummyPeer
        : public Peer
      {
      public:
        DummyPeer(Address id)
        : Peer(id)
        {}
        void
        connect(elle::DurationOpt timeout = elle::DurationOpt()) override
        {
        }
        void
        reconnect(elle::DurationOpt timeout = elle::DurationOpt()) override
        {
        }
        void
        store(blocks::Block const& block, StoreMode mode) override
        {
           throw reactor::network::Exception("Peer unavailable");
        }
        void
        remove(Address address, blocks::RemoveSignature rs) override
        {
          throw reactor::network::Exception("Peer unavailable");
        }
         std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override
        {
          throw reactor::network::Exception("Peer unavailable");
        }
        void
        print(std::ostream& stream) const override
        {
          elle::fprintf(stream, "%s(%s)", elle::type_info(*this), this->id());
        }
      };
    }
  }
}




#endif
