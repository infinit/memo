#pragma once

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <elle/Error.hh>
#include <elle/athena/paxos/Client.hh>
#include <elle/das/tuple.hh>
#include <elle/reactor/duration.hh>
#include <elle/unordered_map.hh>

#include <memo/model/doughnut/Consensus.hh>
#include <memo/model/doughnut/Local.hh>
#include <memo/model/doughnut/Remote.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        namespace bmi = boost::multi_index;

        ELLE_DAS_SYMBOL(address);
        ELLE_DAS_SYMBOL(block);
        ELLE_DAS_SYMBOL(doughnut);
        ELLE_DAS_SYMBOL(replication_factor);
        ELLE_DAS_SYMBOL(lenient_fetch);
        ELLE_DAS_SYMBOL(rebalance_auto_expand);
        ELLE_DAS_SYMBOL(rebalance_inspect);
        ELLE_DAS_SYMBOL(node);
        ELLE_DAS_SYMBOL(node_timeout);

        struct BlockOrPaxos;

        class Paxos
          : public Consensus
        {
        /*------.
        | Types |
        `------*/
        public:
          using Self = Paxos;
          using Super = Consensus;
          using PaxosClient =
            elle::athena::paxos::Client<std::shared_ptr<blocks::Block>, int, Address>;
          using PaxosServer = elle::athena::paxos::Server<
            std::shared_ptr<blocks::Block>, int, Address> ;
          using Value = elle::Option<std::shared_ptr<blocks::Block>,
                                     PaxosClient::Quorum>;
          using Duration = std::chrono::system_clock::duration;

        /*-------------.
        | Construction |
        `-------------*/
        public:
          Paxos(Doughnut& doughnut,
                int factor,
                bool lenient_fetch,
                bool rebalance_auto_expand,
                bool rebalance_inspect,
                Duration node_timeout);
          template <typename ... Args>
          Paxos(Args&& ... args);
          ELLE_ATTRIBUTE_R(int, factor);
          ELLE_ATTRIBUTE_R(bool, lenient_fetch);
          ELLE_ATTRIBUTE_R(bool, rebalance_auto_expand);
          ELLE_ATTRIBUTE_R(bool, rebalance_inspect);
          ELLE_ATTRIBUTE_R(Duration, node_timeout);

        /*-------.
        | Blocks |
        `-------*/
        public:
          bool
          rebalance(Address address);
          bool
          rebalance(Address address, PaxosClient::Quorum const& ids);
        protected:
          void
          _store(std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          std::unique_ptr<blocks::Block>
          _fetch(Address address, boost::optional<int> local_version) override;
          void
          _fetch(std::vector<AddressVersion> const& addresses,
                 ReceiveBlock res) override;
          void
          _remove(Address address, blocks::RemoveSignature rs) override;
          bool
          _rebalance(PaxosClient& client, Address address);
          bool
          _rebalance(PaxosClient& client,
                     Address address,
                     std::function<PaxosClient::Quorum (PaxosClient::Quorum)> m,
                     PaxosClient::State const& version);
          PaxosServer::Quorum
          _rebalance_extend_quorum(Address address, PaxosServer::Quorum q);
          void
          _resign() override;

        private:
          PaxosClient
          _client(Address const& addr);
          PaxosClient::State
          _latest(PaxosClient& client, Address address);

        /*--------.
        | Factory |
        `--------*/
        public:
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     boost::optional<boost::asio::ip::address> listen_address,
                     std::unique_ptr<silo::Silo> storage) override;
          virtual
          std::shared_ptr<Remote>
          make_remote(std::shared_ptr<Dock::Connection> connection) override;

          using AcceptedOrError
            = std::pair<boost::optional<PaxosClient::Accepted>,
                        std::shared_ptr<elle::Error>>;
          using GetMultiResult = std::unordered_map<Address, AcceptedOrError>;

        /*------------.
        | Paxos::Peer |
        `------------*/
        public:
          class Peer
            : public virtual doughnut::Peer
          {
          public:
            using Super = doughnut::Peer;
            Peer(Doughnut& dht, model::Address id);
            virtual
            PaxosServer::Response
            propose(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p,
                    bool insert) = 0;
            virtual
            PaxosClient::Proposal
            accept(PaxosServer::Quorum const& peers,
                   Address address,
                   PaxosClient::Proposal const& p,
                   Value const& value) = 0;
            virtual
            void
            confirm(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p) = 0;
            virtual
            boost::optional<PaxosClient::Accepted>
            get(PaxosServer::Quorum const& peers,
                Address address,
                boost::optional<int> local_version) = 0;
            virtual
            bool
            reconcile(Address address) = 0;
            virtual
            void
            propagate(PaxosServer::Quorum  q,
                      std::shared_ptr<blocks::Block> block,
                      PaxosClient::Proposal p) = 0;
          };

        /*------------------.
        | Paxos::RemotePeer |
        `------------------*/
          class RemotePeer
            : public Peer
            , public doughnut::Remote
          {
          public:
            using Super = doughnut::Remote;
            template <typename ... Args>
            RemotePeer(Doughnut& dht,
                       std::shared_ptr<Dock::Connection> connection)
              : doughnut::Peer(dht, connection->location().id())
              , Peer(dht, connection->location().id())
              , Super(dht, std::move(connection))
            {}
            PaxosServer::Response
            propose(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p,
                    bool insert) override;
            PaxosClient::Proposal
            accept(PaxosServer::Quorum const& peers,
                   Address address,
                   PaxosClient::Proposal const& p,
                   Value const& value) override;
            void
            confirm(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p) override;
            boost::optional<PaxosClient::Accepted>
            get(PaxosServer::Quorum const& peers,
                Address address,
                boost::optional<int> local_version) override;
            bool
            reconcile(Address address) override;
            void
            propagate(PaxosServer::Quorum  q,
                      std::shared_ptr<blocks::Block> block,
                      PaxosClient::Proposal p) override;
            void
            store(blocks::Block const& block, StoreMode mode) override;
          };

        /*-----------------.
        | Paxos::LocalPeer |
        `-----------------*/
        public:
          class LocalPeer
            : public Peer
            , public doughnut::Local
          {
          /*-------------.
          | Construction |
          `-------------*/
          public:
            using Self = LocalPeer;
            using Super = doughnut::Local;
            using PaxosClient = Paxos::PaxosClient;
            using PaxosServer = Paxos::PaxosServer;
            using Value = Paxos::Value;
            using Quorum = PaxosServer::Quorum;
            template <typename ... Args>
            LocalPeer(Paxos& paxos,
                      int factor,
                      bool rebalance_auto_expand,
                      bool rebalance_inspect,
                      Duration node_timeout,
                      Doughnut& dht,
                      Address id,
                      Args&& ... args);
            ~LocalPeer() override;
            void
            initialize() override;
            ELLE_ATTRIBUTE_R(Paxos&, paxos);
            ELLE_ATTRIBUTE_R(int, factor);
            ELLE_ATTRIBUTE_RW(bool, rebalance_auto_expand);
            ELLE_ATTRIBUTE_RW(bool, rebalance_inspect);
            ELLE_ATTRIBUTE_R(elle::reactor::Thread::unique_ptr,
                             rebalance_inspector);
            ELLE_ATTRIBUTE_R(Duration, node_timeout);
            ELLE_ATTRIBUTE(std::vector<elle::reactor::Thread::unique_ptr>,
                           evict_threads);
            ELLE_ATTRIBUTE_R(bool, cleaning_up);
          protected:
            void
            _cleanup() override;
          private:
            friend class Paxos;

          /*------.
          | Paxos |
          `------*/
          public:
            PaxosServer::Response
            propose(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p,
                    bool insert) override;
            PaxosClient::Proposal
            accept(PaxosServer::Quorum const& peers,
                   Address address,
                   PaxosClient::Proposal const& p,
                   Value const& value) override;
            void
            confirm(PaxosServer::Quorum const& peers,
                    Address address,
                    PaxosClient::Proposal const& p) override;
            boost::optional<PaxosClient::Accepted>
            get(PaxosServer::Quorum const& peers,
                Address address,
                boost::optional<int> local_version) override;
            bool
            reconcile(Address address) override;
            void
            propagate(PaxosServer::Quorum  q,
                      std::shared_ptr<blocks::Block> block,
                      PaxosClient::Proposal p) override;

            void
            store(blocks::Block const& block, StoreMode mode) override;
            void
            remove(Address address, blocks::RemoveSignature rs) override;
            struct Decision
            {
              Decision(PaxosServer paxos);
              Decision(elle::serialization::SerializerIn& s);
              void
              serialize(elle::serialization::Serializer& s);
              using serialization_tag = memo::serialization_tag;
              int chosen;
              PaxosServer paxos;
            };
            bool
            rebalance(PaxosClient& client, Address address);
          protected:
            std::unique_ptr<blocks::Block>
            _fetch(Address address,
                  boost::optional<int> local_version) const override;
            void
            _register_rpcs(Connection& rpcs) override;
            struct DecisionEntry
            {
              Address address;
              elle::Time use;
              std::shared_ptr<Decision> decision;
            };
            using Addresses = bmi::multi_index_container<
              DecisionEntry,
              bmi::indexed_by<
                bmi::hashed_unique<
                  bmi::member<
                    DecisionEntry,
                    Address,
                    &DecisionEntry::address>>,
                bmi::ordered_unique<
                  bmi::member<
                    DecisionEntry,
                    elle::Time,
                    &DecisionEntry::use>>
              >>;
            ELLE_ATTRIBUTE_R(Addresses, addresses);
            ELLE_ATTRIBUTE_R(int, max_addresses_size);
          private:
            void
            _remove(Address address);
            BlockOrPaxos
            _load(Address address);
            std::shared_ptr<Decision>
            _load_paxos(
              Address address,
              boost::optional<PaxosServer::Quorum> peers = {},
              std::shared_ptr<blocks::Block> value = nullptr);
            std::shared_ptr<Decision>
            _load_paxos(Address address, Decision decision);
            void
            _cache(Address address, bool immutable, Quorum quorum);
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
            ELLE_ATTRIBUTE((elle::reactor::Channel<std::pair<Address, bool>>),
                           rebalancable);
            ELLE_ATTRIBUTE_X(boost::signals2::signal<void(Address)>,
                             rebalanced);
            /// Emitted when a block becomes under-replicated and cannot be
            /// rebalanced. For tests purpose, not emitted in every single case
            /// yet.
            ELLE_ATTRIBUTE_X(boost::signals2::signal<void(Address, int)>,
                             under_replicated);
            ELLE_ATTRIBUTE(elle::reactor::Thread, rebalance_thread);
            struct BlockRepartition
            {
              BlockRepartition(Address address,
                               bool immubable,
                               PaxosServer::Quorum quorum);
              Address address;
              bool immutable;
              PaxosServer::Quorum quorum;
              int
              replication_factor() const;
              struct HashByAddress;
              bool
              operator ==(BlockRepartition const& rhs) const;
            };
            using Quorums = bmi::multi_index_container<
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
                >>;
            /// Blocks quorum
            ELLE_ATTRIBUTE_R(Quorums, quorums);
            /// Nodes blocks
            using NodeBlock = elle::das::tuple<Symbol_node::Formal<Address>,
                                               Symbol_block::Formal<Address>>;
            static
            Address
            node(NodeBlock const& b)
            {
              return b.node;
            }
            static
            Address
            block(NodeBlock const& b)
            {
              return b.block;
            }
            struct by_node {};
            struct by_block {};
            using NodeBlocks = bmi::multi_index_container<
              NodeBlock,
              bmi::indexed_by<
                bmi::hashed_unique<
                  bmi::identity<NodeBlock>>,
                bmi::hashed_non_unique<
                  bmi::tag<by_node>,
                  bmi::global_fun<NodeBlock const&,
                                  Address,
                                  &node>>,
                bmi::hashed_non_unique<
                  bmi::tag<by_block>,
                  bmi::global_fun<NodeBlock const&,
                                  Address,
                                  &block>>>>;
            ELLE_ATTRIBUTE_R(NodeBlocks, node_blocks);
            ELLE_ATTRIBUTE_R(std::unordered_set<Address>, nodes);
            using NodeTimeouts =
              std::unordered_map<Address, elle::reactor::AsioTimer>;
            ELLE_ATTRIBUTE_R(NodeTimeouts, node_timeouts);
          };

          using Transfers = std::unordered_map<Address, int>;
          ELLE_ATTRIBUTE(Transfers, transfers);

        /*-----.
        | Stat |
        `-----*/
        public:
          std::unique_ptr<Consensus::Stat>
          stat(Address const& address) override;

        /*-----------.
        | Monitoring |
        `-----------*/
        public:
          elle::json::Json
          redundancy() override;
          elle::json::Json
          stats() override;

        /*--------------.
        | Configuration |
        `--------------*/
        public:
          class Configuration
            : public consensus::Configuration
          {
          public:
            using Self = memo::model::doughnut::consensus::Paxos::Configuration;
            using Super = consensus::Configuration;
          public:
            Configuration(int replication_factor,
                          Duration node_timeout);
            ELLE_CLONABLE();
            std::unique_ptr<Consensus>
            make(model::doughnut::Doughnut& dht) override;
            ELLE_ATTRIBUTE_RW(int, replication_factor);
            ELLE_ATTRIBUTE_RW(Duration, node_timeout);
            ELLE_ATTRIBUTE_RW(bool, rebalance_auto_expand);
            ELLE_ATTRIBUTE_RW(bool, rebalance_inspect);
          public:
            Configuration(elle::serialization::SerializerIn& s);
            void
            serialize(elle::serialization::Serializer& s) override;
          };

        /*--------.
        | Details |
        `--------*/
        private:
          struct Details;
          friend struct Details;
        };

        struct BlockOrPaxos
        {
          /// A unique_ptr with a changeable deletion function.
          template <typename T>
          using unique_ptr
            = std::unique_ptr<T, std::function<auto (T*) -> void>>;

          explicit
          BlockOrPaxos(blocks::Block& b);
          explicit
          BlockOrPaxos(Paxos::LocalPeer::Decision* p);
          explicit
          BlockOrPaxos(elle::serialization::SerializerIn& s);
          unique_ptr<blocks::Block> block;
          unique_ptr<Paxos::LocalPeer::Decision> paxos;
          void
          serialize(elle::serialization::Serializer& s);
          using serialization_tag = memo::serialization_tag;
        };
      }
    }
  }
}

#include <memo/model/doughnut/consensus/Paxos.hxx>
