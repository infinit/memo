#include <memo/silo/Latency.hh>
#include <memo/model/Address.hh>

#include <elle/factory.hh>

namespace memo
{
  namespace silo
  {
    Latency::Latency(std::unique_ptr<Silo> backend,
                     elle::reactor::DurationOpt latency_get,
                     elle::reactor::DurationOpt latency_set,
                     elle::reactor::DurationOpt latency_erase)
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

    static std::unique_ptr<Silo>
    make(std::vector<std::string> const& args)
    {
      // backend_name, backend_args, latency_get, latency_set, latency_erase;
      auto backend = instantiate(args[0], args[1]);
      auto get = [&](unsigned num) -> elle::Duration
        {
          if (num < args.size())
            return std::chrono::milliseconds(std::stoi(args[num]));
          else
            return {};
        };
      return std::make_unique<Latency>(std::move(backend),
                                       get(2), get(3), get(4));
    }

    struct LatencySiloConfig
      : public SiloConfig
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

      std::unique_ptr<memo::silo::Silo>
      make() override
      {
        return std::make_unique<memo::silo::Latency>(
          storage->make(), latency_get , latency_set, latency_erase);
      }
    };

    static const elle::serialization::Hierarchy<SiloConfig>::
    Register<LatencySiloConfig>
    _register_LatencySiloConfig("latency");
  }
}


FACTORY_REGISTER(memo::silo::Silo, "latency", &memo::silo::make);
