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
          Paxos(Doughnut& doughnut, int factor);
          ELLE_ATTRIBUTE_R(int, factor);

        /*-------.
        | Blocks |
        `-------*/
        protected:
          virtual
          void
          _store(overlay::Overlay& overlay,
                 std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(overlay::Overlay& overlay, Address address) override;
          virtual
          void
          _remove(overlay::Overlay& overlay, Address address) override;
        private:
          reactor::Generator<overlay::Overlay::Member>
          _owners(overlay::Overlay& overlay,
                  Address const& address,
                  overlay::Operation op) const;

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
                   std::shared_ptr<blocks::Block> const& value);
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
                   std::shared_ptr<blocks::Block> const& value);
            virtual
            void
            store(blocks::Block const& block, StoreMode mode) override;
            virtual
            std::unique_ptr<blocks::Block>
            fetch(Address address) const override;
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
          protected:
            virtual
            void
            _register_rpcs(RPCServer& rpcs) override;
            typedef elle::unordered_map<Address, Decision> Addresses;
            ELLE_ATTRIBUTE(Addresses, addresses);
          };

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
