#include <infinit/storage/Storage.hh>

#include <elle/log.hh>
#include <elle/factory.hh>

#include <infinit/storage/Key.hh>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

ELLE_LOG_COMPONENT("infinit.storage.Storage");

namespace infinit
{
  namespace storage
  {
    namespace
    {
      static int const step = 104857600; // 100 Mio
    }

    Storage::Storage(boost::optional<int64_t> capacity)
      : _capacity(std::move(capacity))
      , _usage(0) // _usage is recovered in the child ctor.
      , _base_usage(0)
      , _step(this->capacity() ? (this->capacity().get() / 10) : step)
    {
      // _size_cache too has to be recovered in the child ctor.
    }

    Storage::~Storage()
    {}

    elle::Buffer
    Storage::get(Key key) const
    {
      ELLE_TRACE_SCOPE("%s: get %x", *this, key);
      // FIXME: use _size_cache to check block existance ?
      return this->_get(key);
    }

    int
    Storage::set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_ASSERT(insert || update);
      ELLE_TRACE_SCOPE("%s: %s at %x", *this,
                       insert ? update ? "upsert" : "insert" : "update", key);
      int delta = this->_set(key, value, insert, update);

      _usage += delta;
      if (std::abs(_base_usage - _usage) >= _step)
      {
        ELLE_DUMP("%s: _base_usage - _usage = %s (_step = %s)", *this,
          _base_usage -_usage, _step);
        ELLE_DEBUG("%s: update Beyond (if --push provided) with usage = %s",
          *this, _usage);
        this->_on_storage_size_change();
        _base_usage = _usage;
      }

      ELLE_DEBUG("%s: usage/capacity = %s/%s", *this,
                                               this->_usage,
                                               this->_capacity);
      return delta;
    }

    int
    Storage::erase(Key key)
    {
      ELLE_TRACE_SCOPE("%s: erase %x", *this, key);
      int delta = this->_erase(key);
      ELLE_DEBUG("usage %s and delta %s", _usage, delta);
      _usage += delta;
      this->_size_cache.erase(key);
      return delta;
    }

    std::vector<Key>
    Storage::list()
    {
      ELLE_TRACE_SCOPE("%s: list", *this);
      return this->_list();
    }

    BlockStatus
    Storage::status(Key k)
    {
      ELLE_TRACE_SCOPE("%s: status %x", *this, k);
      return this->_status(k);
    }

    BlockStatus
    Storage::_status(Key k)
    {
      return BlockStatus::unknown;
    }

    void
    Storage::register_notifier(std::function<void ()> f)
    {
      _on_storage_size_change.connect(f);
    }

    /*----------.
    | Printable |
    `----------*/

    void
    Storage::print(std::ostream& out) const
    {
      elle::fprintf(out, "%s(%x)", elle::type_info(*this), this);
    }

    std::unique_ptr<Storage>
    instantiate(std::string const& name,
                std::string const& args)
    {
      ELLE_TRACE_SCOPE("Processing backend %s '%s'", args[0], args[1]);
      std::vector<std::string> bargs;
      size_t space = args.find(" ");
      const char* sep = (space == args.npos) ? ":" : " ";
      boost::algorithm::split(bargs, args, boost::algorithm::is_any_of(sep),
                              boost::algorithm::token_compress_on);
      std::unique_ptr<Storage> backend = elle::Factory<Storage>::instantiate(name, bargs);
      return std::move(backend);
    }

    /*---------------.
    | Storage Config |
    `---------------*/

    StorageConfig::StorageConfig(std::string name_,
                                 boost::optional<int64_t> capacity_)
      : name(std::move(name_))
      , capacity(std::move(capacity_))
    {}

    StorageConfig::StorageConfig(elle::serialization::SerializerIn& s)
    {
      this->serialize(s);
    }

    void
    StorageConfig::serialize(elle::serialization::Serializer& s)
    {
      s.serialize("name", this->name);
      s.serialize("capacity", this->capacity);
    }
  }
}
