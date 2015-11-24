#ifndef INFINIT_STORAGE_ASYNC_HH
#define INFINIT_STORAGE_ASYNC_HH

#include <deque>
#include <reactor/scheduler.hh>
#include <reactor/Barrier.hh>
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
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      int
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;
      virtual
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
      reactor::Barrier _dequeueing;
      reactor::Barrier _queueing;
      std::vector<std::unique_ptr<reactor::Thread>> _threads;
      struct Entry
      {
        Key key;
        Operation operation;
        elle::Buffer data;
        unsigned int hop; // number of times entry was invalidated and pushed back
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


#endif
