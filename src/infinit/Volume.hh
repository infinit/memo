#pragma once

#include <reactor/filesystem.hh>

#include <infinit/MountOptions.hh>
#include <infinit/model/doughnut/Doughnut.hh>

namespace infinit
{
  struct Volume
    : public descriptor::TemplatedBaseDescriptor<Volume>
  {
    Volume(descriptor::BaseDescriptor::Name name,
           std::string network,
           MountOptions const& mount_options,
           boost::optional<std::string> default_permissions,
           boost::optional<std::string> description,
           boost::optional<int> block_size);

    Volume(elle::serialization::SerializerIn& s);

    void
    serialize(elle::serialization::Serializer& s) override;

    std::unique_ptr<reactor::filesystem::FileSystem>
    run(std::shared_ptr<model::doughnut::Doughnut> dht,
        boost::optional<std::string> mountpoint_ = {},
        boost::optional<bool> readonly = {},
        bool allow_root_creation = false,
        bool map_other_permissions = true
#if defined(INFINIT_MACOSX) || defined(INFINIT_WINDOWS)
        , boost::optional<std::string> volname_ = {}
#endif
#ifdef INFINIT_MACOSX
        , boost::optional<std::string> volicon_ = {}
#endif
      );

    boost::filesystem::path
    root_block_cache_dir();

    void
    print(std::ostream& out) const override;

    std::string network;
    MountOptions mount_options;
    boost::optional<std::string> default_permissions;
    boost::optional<int> block_size;
    using serialization_tag = infinit::serialization_tag;
  };
}
