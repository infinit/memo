#pragma once

#include <reactor/network/exception.hh>

#include <infinit/model/doughnut/consensus/Paxos.hh>

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

        std::vector<cryptography::rsa::PublicKey>
        _resolve_keys(std::vector<int> const& ids) override
        {
          elle::unreachable();
        }

        std::unordered_map<int, cryptography::rsa::PublicKey>
        _resolve_all_keys() override
        {
          elle::unreachable();
        }

        boost::optional<consensus::Paxos::PaxosClient::Accepted>
        propose(consensus::Paxos::PaxosServer::Quorum const& peers,
                Address address,
                consensus::Paxos::PaxosClient::Proposal const& p) override
        {
          throw athena::paxos::Unavailable();
        }

        consensus::Paxos::PaxosClient::Proposal
        accept(consensus::Paxos::PaxosServer::Quorum const& peers,
               Address address,
               consensus::Paxos::PaxosClient::Proposal const& p,
               consensus::Paxos::Value const& value) override
        {
          throw athena::paxos::Unavailable();
        }

        void
        confirm(consensus::Paxos::PaxosServer::Quorum const& peers,
                Address address,
                consensus::Paxos::PaxosClient::Proposal const& p) override
        {

        }

        boost::optional<consensus::Paxos::PaxosClient::Accepted>
        get(consensus::Paxos::PaxosServer::Quorum const& peers,
            Address address,
            boost::optional<int> local_version) override
        {
          throw athena::paxos::Unavailable();
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
