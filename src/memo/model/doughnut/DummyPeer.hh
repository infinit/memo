#pragma once

#include <elle/reactor/network/Error.hh>

#include <memo/model/doughnut/consensus/Paxos.hh>

namespace memo
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
          throw elle::reactor::network::Error("Peer unavailable");
        }

        void
        remove(Address address, blocks::RemoveSignature rs) override
        {
          throw elle::reactor::network::Error("Peer unavailable");
        }

        std::unique_ptr<blocks::Block>
        _fetch(Address address,
               boost::optional<int> local_version) const override
        {
          throw elle::reactor::network::Error("Peer unavailable");
        }

        std::vector<elle::cryptography::rsa::PublicKey>
        _resolve_keys(std::vector<int> const& ids) override
        {
          elle::unreachable();
        }

        std::unordered_map<int, elle::cryptography::rsa::PublicKey>
        _resolve_all_keys() override
        {
          elle::unreachable();
        }

        boost::optional<consensus::Paxos::PaxosClient::Accepted>
        propose(consensus::Paxos::PaxosServer::Quorum const&,
                Address,
                consensus::Paxos::PaxosClient::Proposal const&,
                bool) override
        {
          throw elle::athena::paxos::Unavailable();
        }

        bool
        reconcile(Address) override
        {
          throw elle::reactor::network::Error("Peer unavailable");
        }

        consensus::Paxos::PaxosClient::Proposal
        accept(consensus::Paxos::PaxosServer::Quorum const& peers,
               Address address,
               consensus::Paxos::PaxosClient::Proposal const& p,
               consensus::Paxos::Value const& value) override
        {
          throw elle::athena::paxos::Unavailable();
        }

        void
        confirm(consensus::Paxos::PaxosServer::Quorum const& peers,
                Address address,
                consensus::Paxos::PaxosClient::Proposal const& p) override
        {}

        boost::optional<consensus::Paxos::PaxosClient::Accepted>
        get(consensus::Paxos::PaxosServer::Quorum const& peers,
            Address address,
            boost::optional<int> local_version) override
        {
          throw elle::athena::paxos::Unavailable();
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
