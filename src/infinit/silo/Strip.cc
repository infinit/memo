#include <infinit/silo/Strip.hh>

#include <elle/algorithm.hh>

#include <infinit/model/Address.hh>

#include <boost/algorithm/string.hpp>

#include <elle/factory.hh>
#include <elle/reactor/Scope.hh>

namespace infinit
{
  namespace silo
  {
    Strip::Strip(std::vector<std::unique_ptr<Silo>> backend)
      : _backend(std::move(backend))
    {
      // This assumes that the metrics are already correct in the
      // "backends".
      for (auto const& b: _backend)
      {
        _usage += b->usage();
        _block_count += b->block_count();
      }
    }

    elle::Buffer
    Strip::_get(Key k) const
    {
      return _storage_of(k).get(k);
    }

    int
    Strip::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      return _storage_of(k).set(k, value, insert, update);
    }

    int
    Strip::_erase(Key k)
    {
      return _storage_of(k).erase(k);
    }

    namespace
    {
      /// The sum of the digits of this address.
      int
      sum(Key k)
      {
        int res = 0;
        for (auto c: k.value())
          res += c;
        return res;
      }
    }

    Silo&
    Strip::_storage_of(Key k) const
    {
      return *_backend[sum(k) % _backend.size()];
    }

    std::vector<Key>
    Strip::_list()
    {
      auto res = std::vector<Key>{};
      for (auto const& b: _backend)
        elle::push_back(res, b->list());
      return res;
    }

    static
    std::unique_ptr<Silo>
    make(std::vector<std::string> const& args)
    {
      auto backends = std::vector<std::unique_ptr<Silo>>{};
      for (unsigned int i = 0; i < args.size(); i += 2)
        backends.emplace_back(instantiate(args[i], args[i+1]));
      return std::make_unique<Strip>(std::move(backends));
    }


    StripSiloConfig::StripSiloConfig(
      Silos storages,
      boost::optional<int64_t> capacity,
      boost::optional<std::string> description)
      : SiloConfig(
          "multi-storage", std::move(capacity), std::move(description))
      , storage(std::move(storages))
    {}

    StripSiloConfig::StripSiloConfig(elle::serialization::SerializerIn& s)
      : SiloConfig(s)
      , storage(s.deserialize<Silos>("backend"))
    {}

    void
    StripSiloConfig::serialize(elle::serialization::Serializer& s)
    {
      SiloConfig::serialize(s);
      s.serialize("backend", this->storage);
    }

    std::unique_ptr<infinit::silo::Silo>
    StripSiloConfig::StripSiloConfig::make()
    {
      std::vector<std::unique_ptr<infinit::silo::Silo>> s;
      for(auto const& c: storage)
        s.push_back(c->make());
      return std::make_unique<infinit::silo::Strip>(
        std::move(s));
    }

    static const elle::serialization::Hierarchy<SiloConfig>::
    Register<StripSiloConfig>
    _register_StripSiloConfig("strip");
  }
}

FACTORY_REGISTER(infinit::silo::Silo, "strip", &infinit::silo::make);
