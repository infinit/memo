#include <infinit/storage/Mirror.hh>
#include <infinit/model/Address.hh>

#include <reactor/Scope.hh>

#include <boost/algorithm/string.hpp>

#include <elle/factory.hh>

ELLE_LOG_COMPONENT("infinit.fs.mirror");
namespace infinit
{
  namespace storage
  {
    Mirror::Mirror(std::vector<Storage*> backend, bool balance_reads, bool parallel)
      : _balance_reads(balance_reads)
      , _backend(backend)
      , _read_counter(0)
      , _parallel(parallel)
    {
    }
    elle::Buffer
    Mirror::_get(Key k) const
    {
      const_cast<Mirror*>(this)->_read_counter++;
      int target = _balance_reads? _read_counter % _backend.size() : 0;
      return _backend[target]->get(k);
    }
    void
    Mirror::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      if (_parallel)
      {
        elle::With<reactor::Scope>() << [&] (reactor::Scope& s)
        {
          for (Storage* e: _backend)
          {
            s.run_background("mirror set", [&,e] { e->set(k, value, insert, update);});
          }
          s.wait();
        };
      }
      else
      {
        for (Storage* e: _backend)
        {
          e->set(k, value, insert, update);
        }
      }
    }
    void
    Mirror::_erase(Key k)
    {
      if (_parallel)
      {
        elle::With<reactor::Scope>() << [&] (reactor::Scope& s)
        {
          for (Storage* e: _backend)
          {
            s.run_background("mirror erase", [&,e] { e->erase(k);});
          }
          s.wait();
        };
      }
      else
      {
        for (Storage* e: _backend)
        {
          e->erase(k);
        }
      }
    }

    static std::unique_ptr<Storage> make(std::vector<std::string> const& args)
    {
      std::vector<Storage*> backends;
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
        backends.push_back(backend.release());
      }
      return elle::make_unique<Mirror>(backends, balance_reads, parallel);
    }
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "mirror", &infinit::storage::make);
