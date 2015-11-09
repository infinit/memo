#include "infinit/storage/Async.hh"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <infinit/model/Address.hh>
#include <infinit/storage/MissingKey.hh>

#include <elle/os/environ.hh>
#include <elle/factory.hh>
#include <elle/serialization/binary/SerializerIn.hh>
#include <elle/serialization/binary/SerializerOut.hh>

ELLE_LOG_COMPONENT("infinit.fs.async");

namespace bfs = boost::filesystem;

namespace infinit
{
  namespace storage
  {
    static int _max_entry_hop()
    {
      return std::stoi(elle::os::getenv("INFINIT_ASYNC_MAX_ENTRY_HOPS", "4"));
    }

    static int max_entry_hop = _max_entry_hop();

    static int _n_threads()
    {
      return std::stoi(elle::os::getenv("INFINIT_ASYNC_THREADS", "4"));
    }

    static int n_threads = _n_threads();

    Async::Async(std::unique_ptr<Storage> backend,
                 int max_blocks,
                 int64_t max_size,
                 bool merge,
                 std::string const& journal_dir)
      : _backend(std::move(backend))
      , _max_blocks(max_blocks)
      , _max_size(max_size)
      , _op_offset(0)
      , _blocks(0)
      , _bytes(0)
      , _merge(merge)
      , _terminate(false)
      , _journal_dir(journal_dir)
    {
      if (!_journal_dir.empty())
      {
        bfs::create_directories(_journal_dir);
        _restore_journal();
      }
      _queueing.open();
      for (int i=0; i< n_threads; ++i)
      {
        auto t = elle::make_unique<reactor::Thread>("async deque " + std::to_string(i),
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
      if (!_op_cache.empty())
      {
        ELLE_WARN("ASync flushing %s operations still in cache!"
                 " blocks: %s, size: %s",
                 _op_cache.size(), _blocks, _bytes);
      }
      else
        _dequeueing.open();
      for (auto & t: _threads)
        reactor::wait(*t);
      ELLE_TRACE("...~Async");
    }

    void
    Async::_restore_journal()
    {
      ELLE_TRACE("Restoring journal from %s", _journal_dir);
      bfs::path p(_journal_dir);
      bfs::directory_iterator it(p);
      unsigned int min_id = -1;
      while (it != bfs::directory_iterator())
      {
        min_id = std::min(min_id, (unsigned int)std::stoi(it->path().filename().string()));
        ++it;
      }
      _op_offset = signed(min_id) == -1 ? 0 : min_id;
      it = bfs::directory_iterator(p);
      while (it != bfs::directory_iterator())
      {
        int id = std::stoi(it->path().filename().string());
        bfs::ifstream is(*it);
        elle::serialization::binary::SerializerIn sin(is);
        int c;
        Key k;
        elle::Buffer buf;
        sin.serialize("operation", c);
        sin.serialize("key", k);
        sin.serialize("data", buf);
        Operation op = (Operation)c;
        while (signed(_op_cache.size() + _op_offset) <= id)
          _op_cache.push_back(Entry{Key(), Operation::none, elle::Buffer(), 0});
        _inc(buf.size());
        _op_cache[id - _op_offset] = Entry{k, op, std::move(buf), 0};
        if (_merge)
          _op_index[k] = _op_offset;
        ++it;
      }
    }

    void
    Async::flush()
    {
      reactor::wait(!_dequeueing);
    }

    elle::Buffer
    Async::_get(Key k) const
    {
      auto it = std::find_if(_op_cache.rbegin(), _op_cache.rend(),
        [&](Entry const& a)
        {
          return a.key == k;
        });
      if (it != _op_cache.rend())
      {
        if (it->operation == Operation::erase)
          throw MissingKey(k);
        elle::Buffer const& buf = it->data;
        return elle::Buffer(buf.contents(), buf.size());
      }
      return _backend->get(k);
    }

    void
    Async::_push_op(Key k, elle::Buffer const& buf, Operation op)
    {
      int insert_index = -1;
      _op_cache.push_back(Entry{k, op, elle::Buffer(buf), 0});
      insert_index = _op_cache.size() + _op_offset - 1;
      _inc(buf.size());
      ELLE_DEBUG("inserting %s: %s(%s) at %x",
                 insert_index,
                 op == Operation::set ? "set" : "erase",
                 buf.size(),
                 k);
      if (_merge)
      {
        auto it = _op_index.find(k);
        if (it != _op_index.end())
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
                       index,
                       prev.operation == Operation::set ? "set" : "erase",
                       prev.data.size(),
                       k);
            prev.operation = Operation::none;
            prev.data.reset();
            _op_cache[insert_index - _op_offset].hop = prev.hop + 1;
            auto path = bfs::path(_journal_dir) / std::to_string(index);
            bfs::remove(path);
          }
        }
        _op_index[k] = insert_index;
      }
      if (!_journal_dir.empty())
      {
        bfs::path path = bfs::path(_journal_dir) / std::to_string(insert_index);
        ELLE_DEBUG("creating %s", path);
        bfs::ofstream os(path);
        int cop = (char)op;
        elle::serialization::binary::SerializerOut sout(os);
        sout.serialize("operation", cop);
        sout.serialize("key", k);
        sout.serialize("data", buf);
      }
   }

    void
    Async::_erase(Key k)
    {
      ELLE_DEBUG("queueing erase on %x", k);
      _wait();
      _push_op(k, elle::Buffer(), Operation::erase);
    }

    void
    Async::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      _wait();
      ELLE_DEBUG("queueing set on %x . Cache blocks=%s  bytes=%s", k, _blocks, _bytes);
      _push_op(k, value, Operation::set);
    }

    std::vector<Key>
    Async::_list()
    {
      return _backend->list();
    }

    BlockStatus
    Async::_status(Key k)
    {
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
        reactor::wait(_dequeueing);
        ELLE_DEBUG("worker woken up");
        while (!_op_cache.empty())
        {
          // begin atomic block
          auto& e = _op_cache.front();
          elle::Buffer buf = std::move(e.data);
          Key k = e.key;
          Operation op = e.operation;
          _op_cache.pop_front();
          ++_op_offset;
          unsigned int index = _op_offset - 1;
          if (op == Operation::none)
            continue;
          _dec(buf.size());
          //end atomic block
          ELLE_DEBUG("dequeueing %s on %x. Cache blocks=%s bytes=%s oc=%s",
            (op == Operation::erase) ? "erase" : "set",
            k, _blocks, _bytes, _op_cache.size());

          if (_merge)
            _op_index.erase(k);
          if (op == Operation::erase)
          { // if we merged a set and an erase and the set was a 'create',
            // erase might legitimaly fail
            try
            {
              _backend->erase(k);
            }
            catch(MissingKey const& mk)
            {
              ELLE_TRACE("Erase failed with %s", mk);
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
        if (_terminate)
        {
          ELLE_DEBUG("Terminating.");
          break;
        }
      }
    }
    void
    Async::_inc(int64_t size)
    {
      ++_blocks;
      _bytes += size;
      _dequeueing.open();
      if ( (_max_size != -1 && _max_size < _bytes)
        || _max_blocks != -1 && _max_blocks < _blocks)
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
      {
        _dequeueing.close();
      }
      if ( (_max_size == -1 || _max_size >= _bytes)
        && (_max_blocks == -1 || _max_blocks >= _blocks))
        _queueing.open();
    }
    void
    Async::_wait()
    {
      while (true)
      {
        reactor::wait(_queueing);
        if ( (_max_size == -1 || _max_size >= _bytes)
          && (_max_blocks == -1 || _max_blocks >= _blocks))
          break;
      }
    }
    static std::unique_ptr<infinit::storage::Storage>
    make(std::vector<std::string> const& args)
    {
      std::unique_ptr<Storage> backend = instantiate(args[0], args[1]);
      int max_blocks = 100;
      int64_t max_bytes = -1;
      bool merge = true;
      if (args.size() > 2)
        max_blocks = std::stoi(args[2]);
      if (args.size() > 3)
        max_bytes = std::stol(args[3]);
      if (args.size() > 4)
        merge = std::stol(args[4]);
      return elle::make_unique<Async>(std::move(backend), max_blocks, max_bytes, merge);
    }

    struct AsyncStorageConfig:
    public StorageConfig
    {
    public:
      int64_t max_blocks;
      int64_t max_size;
      boost::optional<bool> merge;
      boost::optional<std::string> journal_dir;
      std::shared_ptr<StorageConfig> storage;
      AsyncStorageConfig(elle::serialization::SerializerIn& input)
      : StorageConfig()
      {
        this->serialize(input);
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

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::Async>(
          std::move(storage->make()), max_blocks, max_size,
          merge ? *merge : true, journal_dir? *journal_dir: "");
      }
    };

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<AsyncStorageConfig>
    _register_AsyncStorageConfig("async");
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "async", &infinit::storage::make);
