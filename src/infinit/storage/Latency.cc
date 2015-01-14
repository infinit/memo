#include "infinit/storage/Latency.hh"

#include <infinit/model/Address.hh>

#include <elle/factory.hh>

namespace infinit
{
  namespace storage
  {
    Latency::Latency(std::unique_ptr<Storage> backend,
                     reactor::Duration latency_get,
                     reactor::Duration latency_set,
                     reactor::Duration latency_erase
                     )
    : _backend(std::move(backend))
    , _latency_get(latency_get)
    , _latency_set(latency_set)
    , _latency_erase(latency_erase)
    {}
    elle::Buffer
    Latency::_get(Key k) const
    {
      reactor::sleep(_latency_get);
      return _backend->get(k);
    }
    void
    Latency::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      reactor::sleep(_latency_set);
      _backend->set(k, value, insert, update);
    }
    void
    Latency::_erase(Key k)
    {
      reactor::sleep(_latency_erase);
      _backend->erase(k);
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
  }
}


FACTORY_REGISTER(infinit::storage::Storage, "latency", &infinit::storage::make);