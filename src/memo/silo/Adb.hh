#pragma once

#include <memo/silo/Key.hh>
#include <memo/silo/Silo.hh>

namespace memo
{
  namespace silo
  {
    class Adb
      : public Silo
    {
    public:
      Adb(std::string const& root);
      std::string
      type() const override { return "adb"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
      ELLE_ATTRIBUTE(std::string, root);
    };

    struct AdbSiloConfig
      : public SiloConfig
    {
      AdbSiloConfig(std::string name,
                       boost::optional<int64_t> capacity,
                       boost::optional<std::string> description);
      AdbSiloConfig(elle::serialization::SerializerIn& input);

      void
      serialize(elle::serialization::Serializer& s) override;

      std::unique_ptr<memo::silo::Silo>
      make() override;

      std::string root;
      std::shared_ptr<SiloConfig> storage;
    };
  }
}
