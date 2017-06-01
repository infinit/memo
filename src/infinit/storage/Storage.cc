#include <infinit/storage/Storage.hh>

#include <boost/algorithm/string/case_conv.hpp>

#include <elle/factory.hh>
#include <elle/find.hh>
#include <elle/log.hh>

#include <infinit/storage/Key.hh>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

ELLE_LOG_COMPONENT("infinit.storage.Storage");

namespace
{
  int const step = 100 * 1024 * 1024; // 100 MiB
}


namespace infinit
{
  namespace storage
  {
    bool
    to_bool(std::string const& s)
    {
      static auto const map = std::unordered_map<std::string, bool>
        {
          {"0",     false},
          {"1",     true},
          {"false", false},
          {"n",     false},
          {"no",    false},
          {"true",  true},
          {"y",     true},
          {"yes",   true},
        };
      if (auto it = elle::find(map, boost::to_lower_copy(s)))
        return it->second;
      else
      {
        ELLE_LOG_COMPONENT("to_bool");
        ELLE_WARN("unexpected boolean value: %s", s);
        return false;
      }
    }

    Storage::Storage(boost::optional<int64_t> capacity)
      : _capacity(std::move(capacity))
      , _usage(0) // recovered in the child ctor.
      , _base_usage(0)
      , _step(this->capacity() ? (this->capacity().get() / 10) : step)
      , _block_count{0} // recovered in the child ctor.
    {
      // _size_cache too has to be recovered in the child ctor.

      // There is no point in notifying about the metrics now, even if
      // ctors of subclasses may update _usage and _block_count, as
      // this piece of code would be executed first anyway.  So let
      // these ctors notify themselves.
    }

    Storage::~Storage()
    {}

    void
    Storage::_notify_metrics()
    {
      try
      {
        this->_on_storage_size_change();
      }
      catch (elle::Error const& e)
      {
        ELLE_WARN("Error notifying storage size change: %s", e);
      }
    }

    elle::Buffer
    Storage::get(Key key) const
    {
      ELLE_TRACE_SCOPE("%s: get %x", this, key);
      // FIXME: use _size_cache to check block existance?
      return this->_get(key);
    }

    int
    Storage::set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      ELLE_ASSERT(insert || update);
      ELLE_TRACE_SCOPE("%s: %s at %x", this,
                       insert ? update ? "upsert" : "insert" : "update", key);
      int delta = this->_set(key, value, insert, update);

      this->_usage += delta;
      if (std::abs(this->_base_usage - this->_usage) >= this->_step)
      {
        ELLE_DUMP("%s: _base_usage - _usage = %s (_step = %s)", this,
          this->_base_usage - this->_usage, this->_step);
        ELLE_DEBUG("%s: update Beyond (if --push provided) with usage = %s",
          this, this->_usage);
        _notify_metrics();
        this->_base_usage = this->_usage;
      }

      ELLE_DEBUG("%s: usage/capacity = %s/%s", this,
                                               this->_usage,
                                               this->_capacity);
      _notify_metrics();
      return delta;
    }

    int
    Storage::erase(Key key)
    {
      ELLE_TRACE_SCOPE("%s: erase %x", this, key);
      int delta = this->_erase(key);
      ELLE_DEBUG("usage %s and delta %s", this->_usage, delta);
      this->_usage += delta;
      this->_size_cache.erase(key);
      _notify_metrics();
      return delta;
    }

    std::vector<Key>
    Storage::list()
    {
      ELLE_TRACE_SCOPE("%s: list", this);
      return this->_list();
    }

    BlockStatus
    Storage::status(Key k)
    {
      ELLE_TRACE_SCOPE("%s: status %x", this, k);
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
      this->_on_storage_size_change.connect(f);
    }

    namespace
    {
      std::vector<std::string>
      split_arguments(std::string const& args)
      {
        auto res = std::vector<std::string>{};
        auto const space = args.find(" ");
        const char* sep = (space == args.npos) ? ":" : " ";
        boost::algorithm::split(res, args,
                                boost::algorithm::is_any_of(sep),
                                boost::algorithm::token_compress_on);
        return res;
      }
    }

    std::unique_ptr<Storage>
    instantiate(std::string const& name, std::string const& args)
    {
      ELLE_TRACE_SCOPE("Processing backend %s '%s'", name, args);
      return elle::Factory<Storage>::instantiate(name, split_arguments(args));
    }

    /*---------------.
    | Storage Config |
    `---------------*/

    StorageConfig::StorageConfig(std::string name,
                                 boost::optional<int64_t> capacity,
                                 boost::optional<std::string> description)
      : descriptor::TemplatedBaseDescriptor<StorageConfig>(
          std::move(name), std::move(description))
      , capacity(std::move(capacity))
    {}

    StorageConfig::StorageConfig(elle::serialization::SerializerIn& s)
      : descriptor::TemplatedBaseDescriptor<StorageConfig>(s)
      , capacity(s.deserialize<boost::optional<int64_t>>("capacity"))
    {}

    void
    StorageConfig::serialize(elle::serialization::Serializer& s)
    {
      descriptor::TemplatedBaseDescriptor<StorageConfig>::serialize(s);
      s.serialize("capacity", this->capacity);
    }

    std::string
    StorageConfig::name_regex()
    {
      return "^[-a-zA-Z0-9._]{0,127}$";
    }
  }
}
