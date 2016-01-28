#ifndef INFINIT_MODEL_DOUGHNUT_CONSENSUS_PAXOS_HH
# define INFINIT_MODEL_DOUGHNUT_CONSENSUS_PAXOS_HH

# include <elle/unordered_map.hh>

# include <reactor/Generator.hh>

# include <athena/paxos/Client.hh>

# include <infinit/model/doughnut/Consensus.hh>
# include <infinit/model/doughnut/Local.hh>
# include <infinit/model/doughnut/Remote.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        class Paxos
          : public Consensus
        {
        /*------.
        | Types |
        `------*/
        public:
          typedef Paxos Self;
          typedef Consensus Super;

        /*-------------.
        | Construction |
        `-------------*/
        public:
          Paxos(Doughnut& doughnut, int factor, bool lenient_fetch = false);
          ELLE_ATTRIBUTE_R(int, factor);
          ELLE_ATTRIBUTE_R(bool, lenient_fetch);
        private:
          struct _Details;
          friend struct _Details;

        /*-------.
        | Blocks |
        `-------*/
        protected:
          virtual
          void
          _store(std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(Address address, boost::optional<int> local_version) override;
          virtual
          void
          _remove(Address address, blocks::RemoveSignature rs) override;

        /*--------.
        | Factory |
        `--------*/
        public:
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     std::unique_ptr<storage::Storage> storage) override;

        /*-----.
        | Peer |
        `-----*/
        public:
          typedef
          athena::paxos::Client<std::shared_ptr<blocks::Block>, int, Address>
          PaxosClient;
          typedef athena::paxos::Server<
            std::shared_ptr<blocks::Block>, int, Address>
          PaxosServer;
          typedef elle::Option<std::shared_ptr<blocks::Block>,
                               Paxos::PaxosClient::Quorum> Value;

          class RemotePeer
            : public doughnut::Remote
          {
          public:
            template <typename ... Args>
            RemotePeer(Args&& ... args)
              : doughnut::Remote(std::forward<Args>(args) ...)
            {}

            virtual
            boost::optional<PaxosClient::Accepted>
            propose(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p);
            virtual
            PaxosClient::Proposal
            accept(PaxosServer::Quorum const& peers,
                   Address address,
                   PaxosClient::Proposal const& p,
                   Value const& value);
            virtual
            std::pair<PaxosServer::Quorum,
                      std::unique_ptr<Paxos::PaxosClient::Accepted>>
            _fetch_paxos(Address address);
          };

          class LocalPeer
            : public doughnut::Local
          {
          public:
            template <typename ... Args>
            LocalPeer(int factor, Args&& ... args)
              : doughnut::Local(std::forward<Args>(args) ...)
              , _factor(factor)
            {}

            ELLE_ATTRIBUTE_R(int, factor);

            virtual
            boost::optional<PaxosClient::Accepted>
            propose(PaxosServer::Quorum peers,
                    Address address,
                    PaxosClient::Proposal const& p);
            virtual
            PaxosClient::Proposal
            accept(PaxosServer::Quorum peers,
                   Address address,
                   PaxosClient::Proposal const& p,
                   Value const& value);
            virtual
            void
            store(blocks::Block const& block, StoreMode mode) override;
            virtual
            void
            remove(Address address, blocks::RemoveSignature rs) override;
            struct Decision
            {
              Decision(PaxosServer paxos);
              Decision(elle::serialization::SerializerIn& s);
              void
              serialize(elle::serialization::Serializer& s);
              typedef infinit::serialization_tag serialization_tag;
              int chosen;
              PaxosServer paxos;
            };
            std::pair<PaxosServer::Quorum,
                      std::unique_ptr<Paxos::PaxosClient::Accepted>>
            _fetch_paxos(Address address);
          protected:
            virtual
            std::unique_ptr<blocks::Block>
            _fetch(Address address,
                  boost::optional<int> local_version) const override;
            virtual
            void
            _register_rpcs(RPCServer& rpcs) override;
            typedef elle::unordered_map<Address, Decision> Addresses;
            ELLE_ATTRIBUTE(Addresses, addresses);
          };

          /*-----.
          | Stat |
          `-----*/
          public:
            virtual
            std::unique_ptr<Consensus::Stat>
            stat(Address const& address) override;

          class Configuration
            : public consensus::Configuration
          {
          /*--------.
          | Factory |
          `--------*/
          public:
            Configuration(int replication_factor);
            virtual
            std::unique_ptr<Consensus>
            make(model::doughnut::Doughnut& dht) override;
            ELLE_ATTRIBUTE_R(int, replication_factor);

          /*--------------.
          | Serialization |
          `--------------*/
          public:
            Configuration(elle::serialization::SerializerIn& s);
            virtual
            void
            serialize(elle::serialization::Serializer& s) override;
          };
        };
      }
    }
  }
}

#endif
