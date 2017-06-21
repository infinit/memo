#include <memo/silo/Silo.hh>

#include <boost/algorithm/string/case_conv.hpp>

#include <elle/factory.hh>
#include <elle/find.hh>
#include <elle/log.hh>

#include <memo/silo/Key.hh>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

ELLE_LOG_COMPONENT("memo.storage.Silo");

namespace
{
  int const step = 100 * 1024 * 1024; // 100 MiB
}


namespace memo
{
  namespace silo
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

    Silo::Silo(boost::optional<int64_t> capacity)
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

    Silo::~Silo()
    {}

    void
    Silo::_notify_metrics()
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
    Silo::get(Key key) const
    {
      ELLE_TRACE_SCOPE("%s: get %x", this, key);
      // FIXME: use _size_cache to check block existance?
      return this->_get(key);
    }

    int
    Silo::set(Key key, elle::Buffer const& value, bool insert, bool update)
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
    Silo::erase(Key key)
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
    Silo::list()
    {
      ELLE_TRACE_SCOPE("%s: list", this);
      return this->_list();
    }

    BlockStatus
    Silo::status(Key k)
    {
      ELLE_TRACE_SCOPE("%s: status %x", this, k);
      return this->_status(k);
    }

    BlockStatus
    Silo::_status(Key k)
    {
      return BlockStatus::unknown;
    }

    void
    Silo::register_notifier(std::function<void ()> f)
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

    std::unique_ptr<Silo>
    instantiate(std::string const& name, std::string const& args)
    {
      ELLE_TRACE_SCOPE("Processing backend %s '%s'", name, args);
      return elle::Factory<Silo>::instantiate(name, split_arguments(args));
    }

    /*--------------.
    | Silo Config.  |
    `--------------*/

    SiloConfig::SiloConfig(std::string name,
                                 boost::optional<int64_t> capacity,
                                 boost::optional<std::string> description)
      : descriptor::TemplatedBaseDescriptor<SiloConfig>(
          std::move(name), std::move(description))
      , capacity(std::move(capacity))
    {}

    SiloConfig::SiloConfig(elle::serialization::SerializerIn& s)
      : descriptor::TemplatedBaseDescriptor<SiloConfig>(s)
      , capacity(s.deserialize<boost::optional<int64_t>>("capacity"))
    {}

    void
    SiloConfig::serialize(elle::serialization::Serializer& s)
    {
      descriptor::TemplatedBaseDescriptor<SiloConfig>::serialize(s);
      s.serialize("capacity", this->capacity);
    }

    std::string
    SiloConfig::name_regex()
    {
      return "^[-a-zA-Z0-9._]{0,127}$";
    }
  }
}
