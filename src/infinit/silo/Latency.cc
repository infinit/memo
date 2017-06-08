#include "infinit/silo/Latency.hh"

#include <infinit/model/Address.hh>

#include <elle/factory.hh>

namespace elle
{
  namespace serialization
  {
    template <>
    struct Serialize<elle::reactor::Duration>
    {
      using Type = double;

      static
      double
      convert(elle::reactor::Duration const& d)
      {
        return (double)d.total_microseconds() / 1000000.0;
      }

      static
      elle::reactor::Duration
      convert(double val)
      {
        return boost::posix_time::microseconds(val * 1000000.0);
      }
    };
  }
}

namespace infinit
{
  namespace silo
  {
    Latency::Latency(std::unique_ptr<Silo> backend,
                     elle::reactor::DurationOpt latency_get,
                     elle::reactor::DurationOpt latency_set,
                     elle::reactor::DurationOpt latency_erase
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
        elle::reactor::sleep(*_latency_get);
      return _backend->get(k);
    }

    int
    Latency::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      if (_latency_set)
        elle::reactor::sleep(*_latency_set);
      _backend->set(k, value, insert, update);

      return 0;
    }

    int
    Latency::_erase(Key k)
    {
      if (_latency_erase)
        elle::reactor::sleep(*_latency_erase);
      _backend->erase(k);

      return 0;
    }

    std::vector<Key>
    Latency::_list()
    {
      return _backend->list();
    }

    static std::unique_ptr<infinit::silo::Silo>
    make(std::vector<std::string> const& args)
    {
      std::unique_ptr<Silo> backend = instantiate(args[0], args[1]);
      elle::reactor::Duration latency_get, latency_set, latency_erase;
      if (args.size() > 2)
        latency_get = boost::posix_time::milliseconds(std::stoi(args[2]));
      if (args.size() > 3)
        latency_set = boost::posix_time::milliseconds(std::stoi(args[3]));
      if (args.size() > 4)
        latency_erase = boost::posix_time::milliseconds(std::stoi(args[4]));
      return std::make_unique<Latency>(std::move(backend),
        latency_get, latency_set, latency_erase);
    }

    struct LatencySiloConfig:
    public SiloConfig
    {
    public:
      elle::reactor::DurationOpt latency_get;
      elle::reactor::DurationOpt latency_set;
      elle::reactor::DurationOpt latency_erase;
      std::shared_ptr<SiloConfig> storage;

      LatencySiloConfig(std::string name,
                           boost::optional<int64_t> capacity,
                           boost::optional<std::string> description)
        : SiloConfig(
            std::move(name), std::move(capacity), std::move(description))
      {}

      LatencySiloConfig(elle::serialization::SerializerIn& s)
        : SiloConfig(s)
      {
        this->serialize(s);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        SiloConfig::serialize(s);
        s.serialize("backend", this->storage);
        s.serialize("latency_get", this->latency_get);
        s.serialize("latency_set", this->latency_set);
        s.serialize("latency_erase", this->latency_erase);
      }

      std::unique_ptr<infinit::silo::Silo>
      make() override
      {
        return std::make_unique<infinit::silo::Latency>(
          storage->make(), latency_get , latency_set, latency_erase);
      }
    };

    static const elle::serialization::Hierarchy<SiloConfig>::
    Register<LatencySiloConfig>
    _register_LatencySiloConfig("latency");
  }
}


FACTORY_REGISTER(infinit::silo::Silo, "latency", &infinit::silo::make);
