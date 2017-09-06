#pragma once

#include <functional>
#include <unordered_map>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <elle/reactor/Channel.hh>
#include <elle/reactor/Thread.hh>

#include <elle/optional.hh>

#include <memo/model/doughnut/Consensus.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      namespace fs = boost::filesystem;
      namespace consensus
      {
        ELLE_DAS_SYMBOL(remove_signature);

        namespace bmi = boost::multi_index;
        class Async
          : public StackedConsensus
        {
        public:
          Async(std::unique_ptr<Consensus> backend,
                fs::path journal_dir,
                int max_size = 100);
          ~Async() override;
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     boost::optional<boost::asio::ip::address> listen_address,
                     std::unique_ptr<silo::Silo> storage) override;
          void
          sync(); // wait until last pushed op gets processed

        protected:
          void
          _store(std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;

          std::unique_ptr<blocks::Block>
          _fetch(Address address, boost::optional<int> local_version) override;

          void
          _fetch(std::vector<AddressVersion> const& addresses,
                 std::function<void(Address, std::unique_ptr<blocks::Block>,
                                    std::exception_ptr)> res) override;

          void
          _remove(Address address, blocks::RemoveSignature rs) override;

        /*-----------.
        | Monitoring |
        `-----------*/
        public:
          elle::json::Object
          redundancy() override;
          elle::json::Object
          stats() override;

        public:
          static
          std::vector<fs::path>
          entries(fs::path const& root);

          /*----------.
          | Operation |
          `----------*/
        public:
          struct Op
          {
            using serialization_tag = memo::serialization_tag;
            Op() = default;
            Op(Address addr_,
               std::unique_ptr<blocks::Block>&& block_,
               boost::optional<StoreMode> mode_ = {},
               std::unique_ptr<ConflictResolver> resolver_ = {},
               blocks::RemoveSignature remove_signature_ = {}
               );
            explicit
            Op(elle::serialization::SerializerIn& ser);
            Op(Op && b);
            void operator = (Op && b);
            void serialize(elle::serialization::Serializer& ser);
            Address address;
            std::unique_ptr<blocks::Block> block;
            boost::optional<StoreMode> mode;
            std::unique_ptr<ConflictResolver> resolver;
            blocks::RemoveSignature remove_signature;
            int index;
            int version;
            using Model = elle::das::Model<
              Op,
              decltype(elle::meta::list(symbols::address,
                                        symbols::block,
                                        symbols::mode,
                                        symbols::resolver,
                                        remove_signature))>;
          };

        private:
          std::unique_ptr<blocks::Block>
          _fetch_cache(Address address, boost::optional<int> local_version,
                       bool& hit);
          void
          _process_loop();
          void
          _process_operation(elle::generic_unique_ptr<Op const> op);
          void
          _init();
          void
          _push_op(Op op);
          Async::Op
          _load_op(fs::path const& path, bool signature = true);
          Async::Op
          _load_op(int id, bool signature = true);
          void
          _load_operations();
          using Operations = bmi::multi_index_container<
            Op,
            bmi::indexed_by<
              bmi::hashed_non_unique<
                bmi::member<Op, Address, &Op::address> >,
              bmi::ordered_unique<
                bmi::member<Op, int, &Op::index> >
            > >;
          ELLE_ATTRIBUTE(Operations, operations);
          ELLE_ATTRIBUTE(elle::reactor::Channel<int>, queue);
          ELLE_ATTRIBUTE(int, next_index);
          ELLE_ATTRIBUTE(int, last_processed_index);
          ELLE_ATTRIBUTE(fs::path, journal_dir);
          /// Index of the first operation stored on disk because memory is at
          /// capacity.
          ELLE_ATTRIBUTE(boost::optional<int>, first_disk_index);
          /// Background loop processing asynchronous operations.
          ELLE_ATTRIBUTE(bool, exit_requested);
          ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr, process_thread);
          ELLE_ATTRIBUTE(elle::reactor::Thread::unique_ptr, init_thread);
          ELLE_ATTRIBUTE(elle::reactor::Barrier, init_barrier);
          ELLE_ATTRIBUTE(bool, in_push);
          ELLE_ATTRIBUTE(std::vector<Op>, reentered_ops);
          ELLE_ATTRIBUTE_R(unsigned long, processed_op_count);
          void
          print_queue();
        };

        std::ostream&
        operator <<(std::ostream& o, Async::Op const& op);
      }
    }
  }
}
