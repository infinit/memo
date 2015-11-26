#ifndef INFINIT_STORAGE_STRIP_HH
#define INFINIT_STORAGE_STRIP_HH

#include <infinit/storage/Storage.hh>
namespace infinit
{
  namespace storage
  {
    /** Balance blocks on the list of specified backend storage.
     * @warning: The same list must be passed each time, in the same order.
    */
    class Strip: public Storage
    {
    public:
      Strip(std::vector<std::unique_ptr<Storage>> backend);
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
      ELLE_ATTRIBUTE(std::vector<std::unique_ptr<Storage>>, backend);
      int _disk_of(Key k) const ;
    };

    struct StripStorageConfig
      : public StorageConfig
    {
    public:
      typedef std::vector<std::unique_ptr<StorageConfig>> Storages;
      StripStorageConfig(Storages storages_, int64_t capacity = 0);
      StripStorageConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override;
      Storages storage;
    };
  }
}

#endif
