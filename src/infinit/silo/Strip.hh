#pragma once

#include <infinit/silo/Silo.hh>

namespace infinit
{
  namespace silo
  {
    /// Balance blocks on the list of specified backend storages.
    /// This is really sharding actually.
    /// 
    /// @warning The same list must be passed each time, in the same
    /// order.
    class Strip
      : public Storage
    {
    public:
      Strip(std::vector<std::unique_ptr<Storage>> backend);
      std::string
      type() const override { return "strip"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
      ELLE_ATTRIBUTE(std::vector<std::unique_ptr<Storage>>, backend);
      /// The storage holding k.
      Storage& _storage_of(Key k) const;
    };

    struct StripStorageConfig
      : public StorageConfig
    {
    public:
      using Storages = std::vector<std::unique_ptr<StorageConfig>>;
      StripStorageConfig(Storages storages,
                         boost::optional<int64_t> capacity = {},
                         boost::optional<std::string> description = {});
      StripStorageConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<infinit::silo::Storage>
      make() override;
      Storages storage;
    };
  }
}
