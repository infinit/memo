#include <infinit/storage/Strip.hh>
#include <infinit/model/Address.hh>

#include <reactor/Scope.hh>

namespace infinit
{
  namespace storage
  {
    Strip::Strip(std::vector<Storage*> backend)
      : _backend(backend)
    {
    }
    elle::Buffer
    Strip::_get(Key k) const
    {
      return _backend[_disk_of(k)]->get(k);
    }
    void
    Strip::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      _backend[_disk_of(k)]->set(k, value, insert, update);
    }
    void
    Strip::_erase(Key k)
    {
      _backend[_disk_of(k)]->erase(k);
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
  }
}