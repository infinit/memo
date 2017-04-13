#include <infinit/storage/Strip.hh>
#include <infinit/model/Address.hh>

#include <boost/algorithm/string.hpp>

#include <elle/factory.hh>
#include <elle/reactor/Scope.hh>

namespace infinit
{
  namespace storage
  {
    Strip::Strip(std::vector<std::unique_ptr<Storage>> backend)
      : _backend(std::move(backend))
    {
    }

    elle::Buffer
    Strip::_get(Key k) const
    {
      return _backend[_disk_of(k)]->get(k);
    }

    int
    Strip::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      _backend[_disk_of(k)]->set(k, value, insert, update);
      // FIXME: impl.
      return 0;
    }

    int
    Strip::_erase(Key k)
    {
      _backend[_disk_of(k)]->erase(k);
      // FIXME: impl.
      return 0;
    }

    int
    Strip::_disk_of(Key k) const
    {
      auto value = k.value();
      int res = 0;
      for (unsigned i=0; i<sizeof(Key::Value); ++i)
        res += value[i];
      return res % _backend.size();
    }

    std::vector<Key>
    Strip::_list()
    {
      std::vector<Key> res = _backend.front()->list();
      for (unsigned i=1; i<_backend.size(); ++i)
      {
        auto keys = _backend[i]->list();
        res.insert(res.end(), keys.begin(), keys.end());
      }
      return res;
    }

    static
    std::unique_ptr<Storage>
    make(std::vector<std::string> const& args)
    {
      auto backends = std::vector<std::unique_ptr<Storage>>{};
      for (unsigned int i = 0; i < args.size(); i += 2)
        backends.emplace_back(instantiate(args[i], args[i+1]));
      return std::make_unique<Strip>(std::move(backends));
    }


    StripStorageConfig::StripStorageConfig(
      Storages storages,
      boost::optional<int64_t> capacity,
      boost::optional<std::string> description)
      : StorageConfig(
          "multi-storage", std::move(capacity), std::move(description))
      , storage(std::move(storages))
    {}

    StripStorageConfig::StripStorageConfig(elle::serialization::SerializerIn& s)
      : StorageConfig(s)
      , storage(s.deserialize<Storages>("backend"))
    {}

    void
    StripStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("backend", this->storage);
    }

    std::unique_ptr<infinit::storage::Storage>
    StripStorageConfig::StripStorageConfig::make()
    {
      std::vector<std::unique_ptr<infinit::storage::Storage>> s;
      for(auto const& c: storage)
        s.push_back(c->make());
      return std::make_unique<infinit::storage::Strip>(
        std::move(s));
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<StripStorageConfig>
    _register_StripStorageConfig("strip");
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "strip", &infinit::storage::make);
