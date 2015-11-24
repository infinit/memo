#include <infinit/storage/Mirror.hh>
#include <infinit/model/Address.hh>

#include <reactor/Scope.hh>

#include <boost/algorithm/string.hpp>

#include <elle/factory.hh>

ELLE_LOG_COMPONENT("infinit.storage.Mirror");

namespace infinit
{
  namespace storage
  {
    Mirror::Mirror(std::vector<std::unique_ptr<Storage>> backend, bool balance_reads, bool parallel)
      : _balance_reads(balance_reads)
      , _backend(std::move(backend))
      , _read_counter(0)
      , _parallel(parallel)
    {}

    elle::Buffer
    Mirror::_get(Key k) const
    {
      const_cast<Mirror*>(this)->_read_counter++;
      int target = _balance_reads? _read_counter % _backend.size() : 0;
      return _backend[target]->get(k);
    }

    int
    Mirror::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      if (_parallel)
      {
        elle::With<reactor::Scope>() << [&] (reactor::Scope& s)
        {
          for (auto& e: _backend)
          {
            Storage* ptr = e.get();
            s.run_background("mirror set", [&,ptr] { ptr->set(k, value, insert, update);});
          }
          s.wait();
        };
      }
      else
      {
        for (auto& e: _backend)
        {
          e->set(k, value, insert, update);
        }
      }

      return 0;
    }

    int
    Mirror::_erase(Key k)
    {
      if (_parallel)
      {
        elle::With<reactor::Scope>() << [&] (reactor::Scope& s)
        {
          for (auto& e: _backend)
          {
            Storage* ptr = e.get();
            s.run_background("mirror erase", [&,ptr] { ptr->erase(k);});
          }
          s.wait();
        };
      }
      else
      {
        for (auto& e: _backend)
        {
          e->erase(k);
        }
      }

      return 0;
    }

    std::vector<Key>
    Mirror::_list()
    {
      return _backend.front()->list();
    }

    static std::unique_ptr<Storage> make(std::vector<std::string> const& args)
    {
      std::vector<std::unique_ptr<Storage>> backends;
      bool balance_reads = args[0] == "true" || args[0] == "1" || args[0] =="yes";
      bool parallel = args[1] == "true" || args[1] == "1" || args[1] =="yes";
      for (int i = 2; i < signed(args.size()); i += 2)
      {
        std::string name = args[i];
        std::vector<std::string> bargs;
        ELLE_TRACE_SCOPE("Processing mirror backend %s '%s'", name, args[i+1]);
        size_t space = args[i+1].find(" ");
        const char* sep = (space == args[i+1].npos) ? ":" : " ";
        boost::algorithm::split(bargs, args[i+1], boost::algorithm::is_any_of(sep),
                                boost::algorithm::token_compress_on);
        std::unique_ptr<Storage> backend = elle::Factory<Storage>::instantiate(name, bargs);
        backends.push_back(std::move(backend));
      }
      return elle::make_unique<Mirror>(std::move(backends), balance_reads, parallel);
    }

    struct MirrorStorageConfig
      : public StorageConfig
    {
      bool parallel;
      bool balance;
      std::vector<std::unique_ptr<StorageConfig>> storage;

      MirrorStorageConfig(std::string name, int capacity = 0)
        : StorageConfig(std::move(name), std::move(capacity))
      {}

      MirrorStorageConfig(elle::serialization::SerializerIn& input)
      : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        StorageConfig::serialize(s);
        s.serialize("parallel", this->parallel);
        s.serialize("balance", this->balance);
        s.serialize("backend", this->storage);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        std::vector<std::unique_ptr<infinit::storage::Storage>> s;
        for(auto const& c: storage)
        {
          ELLE_ASSERT(!!c);
          s.push_back(std::move(c->make()));
        }
        return elle::make_unique<infinit::storage::Mirror>(
          std::move(s), balance, parallel);
      }
    };

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<MirrorStorageConfig>
    _register_MirrorStorageConfig("mirror");
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "mirror", &infinit::storage::make);
