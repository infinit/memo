#ifndef INFINIT_MODEL_DOUGHNUT_DUMMY_PEER_HH
# define INFINIT_MODEL_DOUGHNUT_DUMMY_PEER_HH

# include <reactor/network/exception.hh>

# include <infinit/model/doughnut/consensus/Paxos.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class DummyPeer
        : public consensus::Paxos::Peer
      {
      public:
        DummyPeer(Doughnut& dht, Address id)
          : doughnut::Peer(dht, id)
          , Peer(dht, id)
        {}

        void
        connect(elle::DurationOpt timeout = elle::DurationOpt()) override
        {}

        void
        reconnect(elle::DurationOpt timeout = elle::DurationOpt()) override
        {}

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

        virtual
        boost::optional<consensus::Paxos::PaxosClient::Accepted>
        propose(consensus::Paxos::PaxosServer::Quorum const& peers,
                Address address,
                consensus::Paxos::PaxosClient::Proposal const& p) override
        {
          throw consensus::Paxos::PaxosClient::Peer::Unavailable();
        }

        virtual
        consensus::Paxos::PaxosClient::Proposal
        accept(consensus::Paxos::PaxosServer::Quorum const& peers,
               Address address,
               consensus::Paxos::PaxosClient::Proposal const& p,
               consensus::Paxos::Value const& value) override
        {
          throw consensus::Paxos::PaxosClient::Peer::Unavailable();
        }

        virtual
        void
        confirm(consensus::Paxos::PaxosServer::Quorum const& peers,
                Address address,
                consensus::Paxos::PaxosClient::Proposal const& p) override
        {

        }

        virtual
        boost::optional<consensus::Paxos::PaxosClient::Accepted>
        get(consensus::Paxos::PaxosServer::Quorum const& peers,
            Address address,
            boost::optional<int> local_version) override
        {
          throw consensus::Paxos::PaxosClient::Peer::Unavailable();
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
