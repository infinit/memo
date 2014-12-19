#include <infinit/storage/Mirror.hh>
#include <infinit/model/Address.hh>

#include <reactor/Scope.hh>

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
  }
}