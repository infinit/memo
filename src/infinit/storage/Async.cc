#include <infinit/storage/Async.hh>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/os/environ.hh>
#include <elle/factory.hh>
#include <elle/find.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>

#include <infinit/model/Address.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.fs.async");

namespace bfs = boost::filesystem;

namespace
{
  auto const max_entry_hop
    = elle::os::getenv("INFINIT_ASYNC_MAX_ENTRY_HOPS", 4);
  auto const n_threads
    = elle::os::getenv("INFINIT_ASYNC_THREADS", 4);
}

namespace infinit
{
  namespace storage
  {
    std::ostream&
    operator<<(std::ostream& os, Async::Operation op)
    {
      switch (op)
      {
#define CASE(Op)                                \
      case Async::Operation::Op:                \
        return os << #Op
        CASE(erase);
        CASE(none);
        CASE(set);
#undef CASE
      }
    }

    Async::Async(std::unique_ptr<Storage> backend,
                 int max_blocks,
                 int64_t max_size,
                 bool merge,
                 std::string const& journal_dir)
      : _backend(std::move(backend))
      , _max_blocks(max_blocks)
      , _max_size(max_size)
      , _op_offset(0)
      , _merge(merge)
      , _journal_dir(journal_dir)
    {
      // Update our metrics when our backend updates its.
      _backend->register_notifier([this]
                       {
                         this->_update_metrics();
                       });
      if (!_journal_dir.empty())
      {
        bfs::create_directories(_journal_dir);
        _restore_journal();
      }
      _queueing.open();
      for (int i=0; i< n_threads; ++i)
      {
        auto t = std::make_unique<elle::reactor::Thread>("async deque " + std::to_string(i),
          [&]
          {
            try {
              this->_worker();
              ELLE_TRACE("Worker thread normal exit");
            }
            catch(std::exception const& e)
            {
              ELLE_TRACE("Worker thread threw %s", e.what());
              throw;
            }
          });
        _threads.push_back(std::move(t));
      }
    }

    Async::~Async()
    {
      ELLE_TRACE("~Async...");
      _terminate = true;
      if (_op_cache.empty())
        _dequeueing.open();
      else
        ELLE_WARN("ASync flushing %s operations still in cache!"
                 " blocks: %s, size: %s",
                 _op_cache.size(), _blocks, _bytes);
      for (auto& t: _threads)
        elle::reactor::wait(*t);
      ELLE_TRACE("...~Async");
    }

    namespace
    {
      unsigned
      get_id(bfs::directory_entry const& d)
      {
        return std::stou(d.path().filename().string());
      }
    }

    void
    Async::_restore_journal()
    {
      ELLE_TRACE("Restoring journal from %s", _journal_dir);
      auto const path = bfs::path(_journal_dir);
      _op_offset = [&]
        {
          if (bfs::directory_iterator(path) == bfs::directory_iterator())
            return 0u;
          else
          {
            unsigned res = -1;
            for (auto const& p: bfs::directory_iterator(path))
              res = std::min(res, get_id(p));
            return res;
          }
        }();
      for (auto const& p: bfs::directory_iterator(path))
      {
        auto const id = get_id(p);
        while (_op_cache.size() + _op_offset <= id)
          _op_cache.emplace_back(Key(), Operation::none, elle::Buffer(), 0);
        auto&& is = bfs::ifstream(p.path());
        auto sin = elle::serialization::binary::SerializerIn(is);
        auto op = Operation(sin.deserialize<int>("operation"));
        auto k = sin.deserialize<Key>("key");
        auto buf = sin.deserialize<elle::Buffer>("data");
        _inc(buf.size());
        _op_cache[id - _op_offset] = Entry{k, op, std::move(buf), 0};
        if (_merge)
          _op_index[k] = _op_offset;
      }
    }

    void
    Async::flush()
    {
      elle::reactor::wait(!_dequeueing);
    }

    elle::Buffer
    Async::_get(Key k) const
    {
      auto it = std::find_if(_op_cache.rbegin(), _op_cache.rend(),
        [&](Entry const& a)
        {
          return a.key == k;
        });
      if (it == _op_cache.rend())
        return _backend->get(k);
      else
      {
        if (it->operation == Operation::erase)
          throw MissingKey(k);
        return it->data;
      }
    }

    void
    Async::_push_op(Key k, elle::Buffer const& buf, Operation op)
    {
      _op_cache.push_back(Entry{k, op, elle::Buffer(buf), 0});
      int insert_index = _op_cache.size() + _op_offset - 1;
      _inc(buf.size());
      ELLE_DEBUG("inserting %s: %s(%s) at %x",
                 insert_index, op, buf.size(), k);
      if (_merge)
      {
        if (auto it = elle::find(_op_index, k))
        {
          if (max_entry_hop >= 0 &&
              signed(_op_cache[it->second - _op_offset].hop) >= max_entry_hop)
          {
            ELLE_DEBUG("not moving %s, max hop reached", it->second);
          }
          else
          {
            auto index = it->second;
            auto& prev = _op_cache[index - _op_offset];
            _dec(prev.data.size());
            ELLE_DEBUG("deleting %s: %s(%s) at %x",
                       index, prev.operation, prev.data.size(), k);
            prev.operation = Operation::none;
            prev.data.reset();
            _op_cache[insert_index - _op_offset].hop = prev.hop + 1;
            bfs::remove(bfs::path(_journal_dir) / std::to_string(index));
          }
        }
        _op_index[k] = insert_index;
      }
      if (!_journal_dir.empty())
      {
        auto const path = bfs::path(_journal_dir) / std::to_string(insert_index);
        ELLE_DEBUG("creating %s", path);
        auto&& os = bfs::ofstream(path);
        auto sout = elle::serialization::binary::SerializerOut(os);
        sout.serialize("operation", int(op));
        sout.serialize("key", k);
        sout.serialize("data", buf);
      }
    }

    void
    Async::_update_metrics()
    {
      // _erase and _set are expected to return deltas, that are
      // applied by Storage::erase and Storage::set.  But we don't
      // know these deltas, as the operations are asynchronous.
      //
      // Rather, _erase and _set should return 0, so that the metrics
      // are unchanged when operations are queued.  But we hook the
      // metrics notifications from the worker and then propagate
      // them.
      //
      // They are no locking issues, as they are atomics.
      _usage = _backend->usage().load();
      _block_count = _backend->block_count().load();
      _notify_metrics();
    }

    int
    Async::_erase(Key k)
    {
      ELLE_DEBUG("queueing erase on %x", k);
      _wait();
      _push_op(k, elle::Buffer(), Operation::erase);
      // Do not let Storage::erase try to update the size: we don't
      // know yet what the difference will be.  Metrics will be
      // updated by the worker.
      return 0;
    }

    int
    Async::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      _wait();
      ELLE_DEBUG("queueing set on %x . Cache blocks=%s  bytes=%s",
                 k, _blocks, _bytes);
      _push_op(k, value, Operation::set);
      // Do not let Storage::set try to update the size: we don't know
      // yet what the difference will be.  Metrics will be updated by
      // the worker.
      return 0;
    }

    std::vector<Key>
    Async::_list()
    {
      return _backend->list();
    }

    BlockStatus
    Async::_status(Key k)
    {
      // Warning: reverse iteration!
      auto it = std::find_if(_op_cache.rbegin(), _op_cache.rend(),
        [&](Entry const& a)
        {
          return a.key == k;
        });
      if (it != _op_cache.rend())
      {
        if (it->operation == Operation::erase)
          return BlockStatus::missing;
        else
          return BlockStatus::exists;
      }
      return _backend->status(k);
    }

    void
    Async::_worker()
    {
      while (true)
      {
        ELLE_DEBUG("worker waiting...");
        elle::reactor::wait(_dequeueing);
        ELLE_DEBUG("worker woken up");
        while (!_op_cache.empty())
        {
          // begin atomic block
          auto& e = _op_cache.front();
          elle::Buffer buf = std::move(e.data);
          auto const k = e.key;
          auto const op = e.operation;
          _op_cache.pop_front();
          ++_op_offset;
          if (op != Operation::none)
          {
            unsigned int index = _op_offset - 1;
            _dec(buf.size());
            // end atomic block
            ELLE_DEBUG("dequeueing %s on %x. Cache blocks=%s bytes=%s oc=%s",
                       op, k, _blocks, _bytes, _op_cache.size());

            if (_merge)
              _op_index.erase(k);
            if (op == Operation::erase)
            {
              // If we merged a set and an erase and the set was a
              // 'create', erase might legitimately fail.
              try
              {
                _backend->erase(k);
              }
              catch (MissingKey const& e)
              {
                ELLE_TRACE("Erase failed with %s", e);
              }
            }
            else if (op == Operation::set)
              _backend->set(k, buf, true, true);
            if (!_journal_dir.empty())
            {
              auto path = bfs::path(_journal_dir) / std::to_string(index);
              ELLE_DEBUG("deleting %s", path);
              bfs::remove(path);
            }
          }
        }
        if (_terminate)
        {
          ELLE_DEBUG("Terminating.");
          break;
        }
      }
    }

    bool
    Async::_queue_full() const
    {
      return (_max_size != -1 && _max_size < _bytes
              || _max_blocks != -1 && _max_blocks < _blocks);
    }

    void
    Async::_inc(int64_t size)
    {
      ++_blocks;
      _bytes += size;
      _dequeueing.open();
      if (_queue_full())
      {
        ELLE_DEBUG("async limit reached: %s/%s > %s/%s, closing queue.",
          _blocks, _bytes, _max_blocks, _max_size);
        _queueing.close();
      }
    }

    void
    Async::_dec(int64_t size)
    {
      --_blocks;
      _bytes -= size;
      ELLE_ASSERT_GTE(_blocks, 0);
      ELLE_ASSERT_GTE(_bytes, 0);
      if (!_blocks)
        _dequeueing.close();
      if (!_queue_full())
        _queueing.open();
    }

    void
    Async::_wait()
    {
      while (true)
      {
        elle::reactor::wait(_queueing);
        if (!_queue_full())
          break;
      }
    }

    namespace
    {
      std::unique_ptr<infinit::storage::Storage>
      make(std::vector<std::string> const& args)
      {
        std::unique_ptr<Storage> backend = instantiate(args[0], args[1]);
        auto const max_blocks = 2 < args.size() ? std::stoi(args[2]) : 100;
        auto const max_bytes = int64_t{3 < args.size() ? std::stol(args[3]) : -1};
        auto const merge = 4 < args.size() ? std::stol(args[4]) : true;
        return std::make_unique<Async>(std::move(backend),
                                       max_blocks, max_bytes, merge);
      }
    }

    struct AsyncStorageConfig
      : public StorageConfig
    {
    public:
      AsyncStorageConfig(elle::serialization::SerializerIn& s)
        : StorageConfig(s)
      {
        this->serialize(s);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        StorageConfig::serialize(s);
        s.serialize("max_blocks", this->max_blocks);
        s.serialize("max_size", this->max_size);
        s.serialize("merge", this->merge);
        s.serialize("backend", this->storage);
        s.serialize("journal_dir", this->journal_dir);
      }

      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return std::make_unique<infinit::storage::Async>(
          storage->make(), max_blocks, max_size,
          merge.value_or(true),
          journal_dir.value_or(""));
      }

      int64_t max_blocks;
      int64_t max_size;
      boost::optional<bool> merge;
      boost::optional<std::string> journal_dir;
      std::shared_ptr<StorageConfig> storage;
    };

    namespace
    {
      const elle::serialization::Hierarchy<StorageConfig>::
      Register<AsyncStorageConfig>
      _register_AsyncStorageConfig("async");
    }
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "async", &infinit::storage::make);
