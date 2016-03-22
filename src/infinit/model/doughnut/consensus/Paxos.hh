#ifndef INFINIT_MODEL_DOUGHNUT_CONSENSUS_PAXOS_HH
# define INFINIT_MODEL_DOUGHNUT_CONSENSUS_PAXOS_HH

# include <boost/multi_index_container.hpp>
# include <boost/multi_index/hashed_index.hpp>
# include <boost/multi_index/mem_fun.hpp>
# include <boost/multi_index/ordered_index.hpp>

# include <elle/named.hh>
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
        namespace bmi = boost::multi_index;

        NAMED_ARGUMENT(doughnut);
        NAMED_ARGUMENT(replication_factor);
        NAMED_ARGUMENT(lenient_fetch);
        NAMED_ARGUMENT(rebalance_auto_expand);
        NAMED_ARGUMENT(node_timeout);

        class Paxos
          : public Consensus
        {
        /*------.
        | Types |
        `------*/
        public:
          typedef Paxos Self;
          typedef Consensus Super;
          typedef
          athena::paxos::Client<std::shared_ptr<blocks::Block>, int, Address>
          PaxosClient;
          typedef athena::paxos::Server<
            std::shared_ptr<blocks::Block>, int, Address>
          PaxosServer;
          typedef elle::Option<std::shared_ptr<blocks::Block>,
                               Paxos::PaxosClient::Quorum> Value;

        /*-------------.
        | Construction |
        `-------------*/
        public:
          Paxos(Doughnut& doughnut,
                int factor,
                bool lenient_fetch,
                bool rebalance_auto_expand,
                std::chrono::system_clock::duration node_timeout);
          template <typename ... Args>
          Paxos(Args&& ... args);
          ELLE_ATTRIBUTE_R(int, factor);
          ELLE_ATTRIBUTE_R(bool, lenient_fetch);
          ELLE_ATTRIBUTE_R(bool, rebalance_auto_expand);
          ELLE_ATTRIBUTE_R(std::chrono::system_clock::duration, node_timeout);
        private:
          struct _Details;
          friend struct _Details;

        /*-------.
        | Blocks |
        `-------*/
        public:
          bool
          rebalance(Address address);
          bool
          rebalance(Address address, PaxosClient::Quorum const& ids);
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
          bool
          _rebalance(PaxosClient& client, Address address);
          bool
          _rebalance(PaxosClient& client,
                     Address address,
                     PaxosClient::Quorum const& ids,
                     int version);
          Paxos::PaxosServer::Quorum
          _rebalance_extend_quorum(Address address, PaxosServer::Quorum q);
        private:
          PaxosClient::Peers
          _peers(Address const& address,
                 boost::optional<int> local_version = {});
          PaxosClient
          _client(Address const& addr);
          std::pair<PaxosServer::Quorum, int>
          _latest(PaxosClient& client);

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
            void
            confirm(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p);
            virtual
            boost::optional<PaxosClient::Accepted>
            get(PaxosServer::Quorum const& peers,
                Address address,
                boost::optional<int> local_version);
          };

          /*----------.
          | LocalPeer |
          `----------*/
        public:
          class LocalPeer
            : public doughnut::Local
          {
          /*-------------.
          | Construction |
          `-------------*/
          public:
            typedef Paxos::PaxosClient PaxosClient;
            typedef Paxos::PaxosServer PaxosServer;
            typedef Paxos::Value Value;
            typedef PaxosServer::Quorum Quorum;
            template <typename ... Args>
            LocalPeer(Paxos& paxos,
                      int factor,
                      bool rebalance_auto_expand,
                      std::chrono::system_clock::duration node_timeout,
                      Args&& ... args);
            virtual
            ~LocalPeer();
            virtual
            void
            initialize() override;
            virtual
            void
            cleanup() override;
            ELLE_ATTRIBUTE_R(Paxos&, paxos);
            ELLE_ATTRIBUTE_R(int, factor);
            ELLE_ATTRIBUTE_R(bool, rebalance_auto_expand);
            ELLE_ATTRIBUTE_R(reactor::Thread::unique_ptr, rebalance_inspector);
            ELLE_ATTRIBUTE_R(std::chrono::system_clock::duration, node_timeout);
            ELLE_ATTRIBUTE(std::vector<reactor::Thread::unique_ptr>,
                           evict_threads);
          /*------.
          | Paxos |
          `------*/
          public:
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
            confirm(PaxosServer::Quorum peers,
                    Address address,
                    PaxosClient::Proposal const& p);
            virtual
            boost::optional<PaxosClient::Accepted>
            get(PaxosServer::Quorum peers,
                Address address,
                boost::optional<int> local_version);
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
          private:
            Decision&
            _load(Address address,
                  boost::optional<PaxosServer::Quorum> peers = {});
            Decision&
            _load(Address address, Decision decision);
            void
            _cache(Address address, Quorum quorum);
            void
            _discovered(Address id);
            void
            _disappeared(Address id);
            virtual
            void
            _disappeared_schedule_eviction(model::Address id);
          protected:
            void
            _disappeared_evict(Address id);
          private:
            void
            _rebalance();
            ELLE_ATTRIBUTE((reactor::Channel<std::pair<Address, bool>>),
                           rebalancable);
            ELLE_ATTRIBUTE_X(boost::signals2::signal<void(Address)>,
                             rebalanced);
            ELLE_ATTRIBUTE(reactor::Thread, rebalance_thread);
            struct BlockRepartition
            {
              BlockRepartition(Address address, PaxosServer::Quorum quorum);
              Address address;
              PaxosServer::Quorum quorum;
              int
              replication_factor() const;
            };
            typedef bmi::multi_index_container<
              BlockRepartition,
              bmi::indexed_by<
                bmi::hashed_unique<
                  bmi::member<
                    BlockRepartition,
                    Address,
                    &BlockRepartition::address> >,
                bmi::ordered_non_unique<
                  bmi::const_mem_fun<
                    BlockRepartition,
                    int,
                    &BlockRepartition::replication_factor> >
                >> Quorums;

            /// Blocks quorum
            ELLE_ATTRIBUTE_R(Quorums, quorums);
            /// Nodes blocks
            typedef std::unordered_map<
              Address, std::unordered_set<Address>> NodeBlocks;
            ELLE_ATTRIBUTE_R(NodeBlocks, node_blocks);
            ELLE_ATTRIBUTE_R(std::unordered_set<Address>, nodes);
            typedef
              std::unordered_map<Address, boost::asio::deadline_timer>
              NodeTimeouts;
            ELLE_ATTRIBUTE_R(NodeTimeouts, node_timeouts);
          };

        /*-----.
        | Stat |
        `-----*/
        public:
          virtual
          std::unique_ptr<Consensus::Stat>
          stat(Address const& address) override;

        /*--------------.
        | Configuration |
        `--------------*/
        public:
          class Configuration
            : public consensus::Configuration
          {
          // Factory
          public:
            Configuration(int replication_factor);
            virtual
            std::unique_ptr<Consensus>
            make(model::doughnut::Doughnut& dht) override;
            ELLE_ATTRIBUTE_R(int, replication_factor);
          // Serialization
          public:
            Configuration(elle::serialization::SerializerIn& s);
            virtual
            void
            serialize(elle::serialization::Serializer& s) override;
          };
        };

        struct BlockOrPaxos
        {
          BlockOrPaxos(blocks::Block& b);
          BlockOrPaxos(Paxos::LocalPeer::Decision* p);
          BlockOrPaxos(elle::serialization::SerializerIn& s);
          std::unique_ptr<
            blocks::Block, std::function<void(blocks::Block*)>> block;
          std::unique_ptr<
            Paxos::LocalPeer::Decision,
            std::function<void(Paxos::LocalPeer::Decision*)>> paxos;
          void
          serialize(elle::serialization::Serializer& s);
          typedef infinit::serialization_tag serialization_tag;
        };
      }
    }
  }
}

# include <infinit/model/doughnut/consensus/Paxos.hxx>

#endif
