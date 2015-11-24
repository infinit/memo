#ifndef INFINIT_MODEL_DOUGHNUT_ASYNC_HH
# define INFINIT_MODEL_DOUGHNUT_ASYNC_HH

# include <boost/optional.hpp>
# include <infinit/model/doughnut/Consensus.hh>
# include <reactor/Channel.hh>
# include <reactor/thread.hh>
# include <functional>
# include <unordered_map>
# include <sstream>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        class Async
          : public Consensus
        {
        public:
          Async(std::unique_ptr<Consensus> backend,
                boost::filesystem::path journal_dir,
                int max_size = 100);
          std::unique_ptr<Local>
          make_local(boost::optional<int> port,
                     std::unique_ptr<storage::Storage> storage) override;

        protected:
          virtual
          void
          _store(overlay::Overlay& overlay,
                 std::unique_ptr<blocks::Block> block,
                 StoreMode mode,
                 std::unique_ptr<ConflictResolver> resolver) override;
          virtual
          std::unique_ptr<blocks::Block>
          _fetch(overlay::Overlay& overlay,
                 Address address,
                 boost::optional<int>) override;
          virtual
          void
          _remove(overlay::Overlay& overlay,
                  Address address) override;

          /*----------.
          | Operation |
          `----------*/
        public:
          struct Op
          {
            Op() = default;
            Op(Address addr_,
               std::unique_ptr<blocks::Block>&& block_,
               boost::optional<StoreMode> mode_ = {},
               std::unique_ptr<ConflictResolver> resolver_ = {});
            Address address;
            std::unique_ptr<blocks::Block> block;
            boost::optional<StoreMode> mode;
            std::unique_ptr<ConflictResolver> resolver;
            int index;
          };

        private:
          void
          _process_loop();
          void
          _push_op(Op op);
          void
          _restore_journal(overlay::Overlay& overlay);
          ELLE_ATTRIBUTE(std::unique_ptr<Consensus>, backend);
          ELLE_ATTRIBUTE(reactor::Thread::unique_ptr, process_thread);
          ELLE_ATTRIBUTE(reactor::Channel<Op>, ops);
          ELLE_ATTRIBUTE(int, next_index);
          // This map contains for a given address the last version of each
          // block.
          ELLE_ATTRIBUTE((std::unordered_map<Address, blocks::Block*>), last);
          ELLE_ATTRIBUTE(boost::filesystem::path, journal_dir);
          ELLE_ATTRIBUTE(bool, restored_journal);
        };
      }
    }
  }
}

#endif
