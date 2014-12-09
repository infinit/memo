#include <infinit/storage/Key.hh>
#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    elle::Buffer
    Storage::get(Key k) const
    {
      return this->_get(k);
    }

    void
    Storage::set(Key k, elle::Buffer value, bool insert, bool update)
    {
      return this->_set(k, std::move(value), insert, update);
    }
  }
}
