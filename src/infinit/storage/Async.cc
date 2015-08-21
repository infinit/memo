#include "infinit/storage/Async.hh"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <infinit/model/Address.hh>
#include <infinit/storage/MissingKey.hh>

#include <elle/factory.hh>

ELLE_LOG_COMPONENT("infinit.fs.async");

namespace bfs = boost::filesystem;

namespace infinit
{
  namespace storage
  {
    Async::Async(std::unique_ptr<Storage> backend,
                 int max_blocks,
                 int64_t max_size,
                 bool merge,
                 std::string const& journal_dir)
      : _backend(std::move(backend))
      , _max_blocks(max_blocks)
      , _max_size(max_size)
      , _thread("async deque", [&]
        {
          try {
            this->_worker();
            ELLE_TRACE("Worker thread normal exit");
          }
          catch(std::exception const& e)
          {
            ELLE_TRACE("Worker thread threw %s", e.what());
          }
        })
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
      reactor::wait(_thread);
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
      _op_offset = min_id == -1? 0 : min_id;
      it = bfs::directory_iterator(p);
      while (it != bfs::directory_iterator())
      {
        int id = std::stoi(it->path().filename().string());
        bfs::ifstream is(*it);
        char c;
        is.read(&c, 1);
        Key::Value v;
        is.read((char*)v, sizeof(Key::Value));
        Key k(v);
        elle::Buffer buf;
        elle::IOStream output(buf.ostreambuf());
        std::copy(std::istreambuf_iterator<char>(is),
          std::istreambuf_iterator<char>(),
          std::ostreambuf_iterator<char>(output));
        Operation op = (Operation)c;
        while (_op_cache.size() + _op_offset <= id)
          _op_cache.emplace_back(Key(), elle::Buffer(), Operation::none);
        _inc(buf.size());
        _op_cache[id - _op_offset] = std::make_tuple(k, std::move(buf), op);
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
          return std::get<0>(a) == k;
        });
      if (it != _op_cache.rend())
      {
        if (std::get<2>(*it) == Operation::erase)
          throw MissingKey(k);
        elle::Buffer const& buf = std::get<1>(*it);
        return elle::Buffer(buf.contents(), buf.size());
      }
      return _backend->get(k);
    }

    void
    Async::_push_op(Key k, elle::Buffer const& buf, Operation op)
    {
      int insert_index = -1;
      _op_cache.emplace_back(k, elle::Buffer(buf), op);
      insert_index = _op_cache.size() + _op_offset - 1;
      _inc(buf.size());

      if (_merge)
      {
        auto it = _op_index.find(k);
        if (it != _op_index.end())
        {
          auto index = it->second;
          auto& prev = _op_cache[index - _op_offset];
          _dec(std::get<1>(prev).size());
          std::get<2>(prev) = Operation::none;
          std::get<1>(prev).reset();
          auto path = bfs::path(_journal_dir) / std::to_string(index);
          ELLE_DEBUG("deleting %s", path);
          bfs::remove(path);
        }
        _op_index[k] = insert_index;
      }
      if (!_journal_dir.empty())
      {
        bfs::path path = bfs::path(_journal_dir) / std::to_string(insert_index);
        ELLE_DEBUG("creating %s", path);
        bfs::ofstream os(path);
        char cop = (char)op;
        os.write(&cop, 1);
        os.write((const char*)k.value(), sizeof(Key::Value));
        os.write((const char*)buf.contents(), buf.size());
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
          auto& e = _op_cache.front();
          elle::Buffer buf = std::move(std::get<1>(e));
          Key k = std::get<0>(e);
          Operation op = std::get<2>(e);
          ELLE_DEBUG("dequeueing %s on %x. Cache blocks=%s bytes=%s",
                     (op == Operation::erase) ? "erase" : "set",
                     k, _blocks, _bytes);
          _op_cache.pop_front();
          ++_op_offset;
          if (op == Operation::none)
            continue;
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
          _dec(buf.size());
          if (!_journal_dir.empty())
          {
            auto path = bfs::path(_journal_dir) / std::to_string(_op_offset-1);
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
        _queueing.close();
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
      serialize(elle::serialization::Serializer& s)
      {
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
