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
            std::string const& journal_dir="");
      ~Async();
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

      void _worker();
      void _inc(int64_t size);
      void _dec(int64_t size);
      void _wait(); /// Wait for cache to go bellow limit
      void _push_op(Key k, elle::Buffer const& buf, Operation op);
      void _restore_journal();
      std::unique_ptr<Storage> _backend;
      int _max_blocks;
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
      unsigned long _op_offset; // number of popped elements
      int _blocks;
      int _bytes;
      bool _merge; // merge ops to have at most one per key in cache
      bool _terminate;
      std::string _journal_dir;
      std::unordered_map<Key, unsigned long> _op_index;
    };
  }
}
