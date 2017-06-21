#pragma once

#include <boost/filesystem/path.hpp>

#include <memo/silo/Key.hh>
#include <memo/silo/Silo.hh>

namespace memo
{
  namespace silo
  {
    class Filesystem
      : public Silo
    {
    public:
      Filesystem(boost::filesystem::path root,
                 boost::optional<int64_t> capacity = {});
      std::string
      type() const override { return "filesystem"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;
      ELLE_ATTRIBUTE_R(boost::filesystem::path, root);

    private:
      boost::filesystem::path
      _path(Key const& key) const;
    };

    struct FilesystemSiloConfig
      : public SiloConfig
    {
      FilesystemSiloConfig(std::string name,
                              std::string path,
                              boost::optional<int64_t> capacity,
                              boost::optional<std::string> description);
      FilesystemSiloConfig(elle::serialization::SerializerIn& input);
      void
      serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<memo::silo::Silo>
      make() override;
      std::string path;
    };
  }
}
