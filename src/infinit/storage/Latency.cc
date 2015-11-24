#include "infinit/storage/Latency.hh"

#include <infinit/model/Address.hh>

#include <elle/factory.hh>

namespace elle
{
  namespace serialization
  {
    template <>
    struct Serialize<reactor::Duration>
    {
      typedef double Type;

      static
      double
      convert(reactor::Duration const& d)
      {
        return (double)d.total_microseconds() / 1000000.0;
      }

      static
      reactor::Duration
      convert(double val)
      {
        return boost::posix_time::microseconds(val * 1000000.0);
      }
    };
  }
}

namespace infinit
{
  namespace storage
  {
    Latency::Latency(std::unique_ptr<Storage> backend,
                     reactor::DurationOpt latency_get,
                     reactor::DurationOpt latency_set,
                     reactor::DurationOpt latency_erase
                     )
    : _backend(std::move(backend))
    , _latency_get(latency_get)
    , _latency_set(latency_set)
    , _latency_erase(latency_erase)
    {}

    elle::Buffer
    Latency::_get(Key k) const
    {
      if (_latency_get)
        reactor::sleep(*_latency_get);
      return _backend->get(k);
    }

    int
    Latency::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      if (_latency_set)
        reactor::sleep(*_latency_set);
      _backend->set(k, value, insert, update);

      return 0;
    }

    int
    Latency::_erase(Key k)
    {
      if (_latency_erase)
        reactor::sleep(*_latency_erase);
      _backend->erase(k);

      return 0;
    }

    std::vector<Key>
    Latency::_list()
    {
      return _backend->list();
    }

    static std::unique_ptr<infinit::storage::Storage>
    make(std::vector<std::string> const& args)
    {
      std::unique_ptr<Storage> backend = instantiate(args[0], args[1]);
      reactor::Duration latency_get, latency_set, latency_erase;
      if (args.size() > 2)
        latency_get = boost::posix_time::milliseconds(std::stoi(args[2]));
      if (args.size() > 3)
        latency_set = boost::posix_time::milliseconds(std::stoi(args[3]));
      if (args.size() > 4)
        latency_erase = boost::posix_time::milliseconds(std::stoi(args[4]));
      return elle::make_unique<Latency>(std::move(backend),
        latency_get, latency_set, latency_erase);
    }

    struct LatencyStorageConfig:
    public StorageConfig
    {
    public:
      reactor::DurationOpt latency_get;
      reactor::DurationOpt latency_set;
      reactor::DurationOpt latency_erase;
      std::shared_ptr<StorageConfig> storage;

      LatencyStorageConfig(std::string name, int capacity = 0)
        : StorageConfig(std::move(name), std::move(capacity))
      {}

      LatencyStorageConfig(elle::serialization::SerializerIn& input)
      : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        StorageConfig::serialize(s);
        s.serialize("backend", this->storage);
        s.serialize("latency_get", this->latency_get);
        s.serialize("latency_set", this->latency_set);
        s.serialize("latency_erase", this->latency_erase);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::Latency>(
          std::move(storage->make()), latency_get , latency_set, latency_erase);
      }
    };

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<LatencyStorageConfig>
    _register_LatencyStorageConfig("latency");
  }
}


FACTORY_REGISTER(infinit::storage::Storage, "latency", &infinit::storage::make);
