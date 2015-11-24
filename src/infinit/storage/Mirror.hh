#ifndef INFINIT_STORAGE_MIRROR_HH
#define INFINIT_STORAGE_MIRROR_HH

#include <infinit/storage/Storage.hh>
namespace infinit
{
  namespace storage
  {
    class Mirror: public Storage
    {
    public:
      Mirror(std::vector<std::unique_ptr<Storage>> backend, bool balance_reads,
             bool parallel = true);
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      int
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;
      ELLE_ATTRIBUTE(bool, balance_reads);
      ELLE_ATTRIBUTE(std::vector<std::unique_ptr<Storage>>, backend);
      ELLE_ATTRIBUTE(unsigned int, read_counter);
      ELLE_ATTRIBUTE(bool, parallel);
    };
  }
}

#endif
