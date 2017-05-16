#pragma once

#include <infinit/silo/Key.hh>
#include <infinit/silo/Silo.hh>

namespace infinit
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

      std::unique_ptr<infinit::silo::Silo>
      make() override;

      std::string root;
      std::shared_ptr<SiloConfig> storage;
    };
  }
}
