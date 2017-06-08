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
      : public Silo
    {
    public:
      Strip(std::vector<std::unique_ptr<Silo>> backend);
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
      ELLE_ATTRIBUTE(std::vector<std::unique_ptr<Silo>>, backend);
      /// The storage holding k.
      Silo& _storage_of(Key k) const;
    };

    struct StripSiloConfig
      : public SiloConfig
    {
    public:
      using Silos = std::vector<std::unique_ptr<SiloConfig>>;
      StripSiloConfig(Silos storages,
                         boost::optional<int64_t> capacity = {},
                         boost::optional<std::string> description = {});
      StripSiloConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<infinit::silo::Silo>
      make() override;
      Silos storage;
    };
  }
}
