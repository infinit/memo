#include "infinit/storage/Async.hh"

#include <infinit/model/Address.hh>
#include <infinit/storage/MissingKey.hh>

#include <elle/factory.hh>

ELLE_LOG_COMPONENT("infinit.fs.async");

namespace infinit
{
  namespace storage
  {
    Async::Async(std::unique_ptr<Storage> backend,
                 int max_blocks,
                 int64_t max_size,
                 bool merge)
      : _backend(std::move(backend))
      , _max_blocks(max_blocks)
      , _max_size(max_size)
      , _thread("async deque", [&]{this->_worker();})
      , _op_offset(0)
      , _blocks(0)
      , _bytes(0)
      , _merge(merge)
      , _terminate(false)
    {
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
      if (_merge)
      {
        auto it = _op_index.find(k);
        if (it != _op_index.end())
        {
          auto index = it->second;
          ELLE_ASSERT(index >= _op_offset);
          ELLE_ASSERT(index < _op_offset + _op_cache.size());
          auto& prev = _op_cache[index - _op_offset];
          ELLE_ASSERT_EQ(std::get<0>(prev), k);
          _dec(std::get<1>(prev).size());
          std::get<1>(prev).reset();
          std::get<1>(prev).append(buf.contents(), buf.size());
          std::get<2>(prev) = op;
          _inc(buf.size());
          return;
        }
      }
      _op_cache.emplace_back(k, elle::Buffer(buf), op);
      _inc(buf.size());
      if (_merge)
        _op_index.insert(std::make_pair(k, _op_offset + _op_cache.size()-1));
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
          if (_merge)
            _op_index.erase(k);
          _dec(buf.size());
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
        }
        if (_terminate)
          break;
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
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::Async>(
          std::move(storage->make()), max_blocks, max_size,
          merge ? *merge : true);
      }
    };

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<AsyncStorageConfig>
    _register_AsyncStorageConfig("async");


  }
}

FACTORY_REGISTER(infinit::storage::Storage, "async", &infinit::storage::make);
