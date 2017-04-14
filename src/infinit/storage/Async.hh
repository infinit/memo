#pragma once

#include <deque>

#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Barrier.hh>
#include <infinit/storage/Storage.hh>
#include <infinit/model/Address.hh>

namespace infinit
{
  namespace storage
  {
    class Async: public Storage
    {
    public:
      Async(std::unique_ptr<Storage> backend, int max_blocks = 100,
            int64_t max_size = -1, bool merge = true,
            std::string const& journal_dir = {});
      /// Flush the queue.
      ~Async();
      /// Wait for the queue to be empty.
      void
      flush();
      std::string
      type() const override { return "async"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
      BlockStatus
      _status(Key k) override;

    private:
      enum class Operation
      {
        erase,
        set,
        none
      };
      friend
      std::ostream&
      operator<<(std::ostream& os, Operation op);

      /// Flush the operation queue.  Run concurrently in several
      /// threads.
      void _worker();
      void _inc(int64_t size);
      void _dec(int64_t size);
      /// Wait for room in the queue.
      void _wait();
      /// Push an operation in the queue.
      void _push_op(Key k, elle::Buffer const& buf, Operation op);
      /// Load the journal saved on disk in the queue.
      /// Called from the constructor.
      void _restore_journal();
      /// Whether one of the quotas (blocks and bytes) is reached.
      bool _queue_full() const;

      std::unique_ptr<Storage> _backend;
      /// Maximum number of blocks in the queue.  -1 for unlimited.
      int _max_blocks;
      /// Maximum number of bytes in the queue.  -1 for unlimited.
      int64_t _max_size;
      elle::reactor::Barrier _dequeueing;
      elle::reactor::Barrier _queueing;
      std::vector<std::unique_ptr<elle::reactor::Thread>> _threads;
      struct Entry
      {
        Entry(Key k, Operation op, elle::Buffer d, unsigned h)
          : key{k}
          , operation{op}
          , data{d}
          , hop{h}
        {}
        Key key;
        Operation operation;
        elle::Buffer data;
        /// Number of times entry was invalidated and pushed back.
        unsigned int hop;
      };

      std::deque<Entry> _op_cache;
      /// Number of popped elements.
      unsigned long _op_offset;
      /// Number of blocks in the queue.  Controled by _max_blocks.
      int _blocks = 0;
      /// Number of bytes in the queue.  Controled by _max_size.
      int _bytes = 0;
      /// Whether to merge ops applied to keys in the queue.
      bool _merge;
      /// Whether during destruction.
      bool _terminate = false;
      std::string _journal_dir;
      std::unordered_map<Key, unsigned long> _op_index;
    };
  }
}
