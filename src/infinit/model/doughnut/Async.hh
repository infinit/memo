#ifndef INFINIT_MODEL_DOUGHNUT_ASYNC_HH
# define INFINIT_MODEL_DOUGHNUT_ASYNC_HH

# include <functional>
# include <unordered_map>

# include <boost/multi_index_container.hpp>
# include <boost/multi_index/hashed_index.hpp>
# include <boost/multi_index/member.hpp>
# include <boost/multi_index/ordered_index.hpp>

# include <reactor/Channel.hh>
# include <reactor/thread.hh>

# include <elle/optional.hh>

# include <infinit/model/doughnut/Consensus.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        namespace bmi = boost::multi_index;
        class Async
          : public Consensus
        {
        public:
          Async(std::unique_ptr<Consensus> backend,
                boost::filesystem::path journal_dir,
                int max_size = 100);
          ~Async();
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     std::unique_ptr<storage::Storage> storage) override;
          void sync(); // wait until last pushed op gets processed
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
          _fetch(std::vector<AddressVersion> const& addresses,
                 std::function<void(Address, std::unique_ptr<blocks::Block>,
                                    std::exception_ptr)> res) override;
          virtual
          void
          _remove(Address address, blocks::RemoveSignature rs) override;

          /*----------.
          | Operation |
          `----------*/
        public:
          struct Op
          {
            typedef infinit::serialization_tag serialization_tag;
            Op() = default;
            Op(Address addr_,
               std::unique_ptr<blocks::Block>&& block_,
               boost::optional<StoreMode> mode_ = {},
               std::unique_ptr<ConflictResolver> resolver_ = {},
               blocks::RemoveSignature remove_signature_ = {}
               );
            Op(elle::serialization::SerializerIn& ser);
            void serialize(elle::serialization::Serializer& ser);
            Address address;
            std::unique_ptr<blocks::Block> block;
            boost::optional<StoreMode> mode;
            std::unique_ptr<ConflictResolver> resolver;
            blocks::RemoveSignature remove_signature;
            int index;
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
          _load_op(boost::filesystem::path const& path, bool signature = true);
          Async::Op
          _load_op(int id, bool signature = true);
          void
          _load_operations();
          ELLE_ATTRIBUTE(std::unique_ptr<Consensus>, backend);
          typedef bmi::multi_index_container<
            Op,
            bmi::indexed_by<
              bmi::hashed_non_unique<
                bmi::member<Op, Address, &Op::address> >,
              bmi::ordered_unique<
                bmi::member<Op, int, &Op::index> >
            > > Operations;
          ELLE_ATTRIBUTE(Operations, operations);
          ELLE_ATTRIBUTE(reactor::Channel<int>, queue);
          ELLE_ATTRIBUTE(int, next_index);
          ELLE_ATTRIBUTE(int, last_processed_index);
          ELLE_ATTRIBUTE(boost::filesystem::path, journal_dir);
          /// Index of the first operation stored on disk because memory is at
          /// capacity.
          ELLE_ATTRIBUTE(boost::optional<int>, first_disk_index);
          /// Background loop processing asynchronous operations.
          ELLE_ATTRIBUTE(bool, exit_requested);
          ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, process_thread);
          ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, init_thread);
          ELLE_ATTRIBUTE(reactor::Barrier, init_barrier);
          ELLE_ATTRIBUTE(bool, in_push);
          ELLE_ATTRIBUTE(std::vector<Op>, reentered_ops);
        };
      }
    }
  }
}

#endif
