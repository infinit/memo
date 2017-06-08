#include <infinit/Volume.hh>

#include <elle/algorithm.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("infinit.Volume");

namespace infinit
{
  namespace bfs = boost::filesystem;

  Volume::Volume(descriptor::BaseDescriptor::Name name,
                 std::string network,
                 MountOptions const& mount_options,
                 boost::optional<std::string> default_permissions,
                 boost::optional<std::string> description,
                 boost::optional<int> block_size)
    : descriptor::TemplatedBaseDescriptor<Volume>(std::move(name),
                                                  std::move(description))
    , network(std::move(network))
    , mount_options(mount_options)
    , default_permissions(std::move(default_permissions))
    , block_size(std::move(block_size))
  {}

  Volume::Volume(elle::serialization::SerializerIn& s)
    : descriptor::TemplatedBaseDescriptor<Volume>(s)
  {
    this->serialize(s);
  }

  void
  Volume::serialize(elle::serialization::Serializer& s)
  {
    descriptor::TemplatedBaseDescriptor<Volume>::serialize(s);
    s.serialize("network", this->network);
    s.serialize("default_permissions", this->default_permissions);
    s.serialize("block_size", this->block_size);
    try
    {
      s.serialize("mount_options", this->mount_options);
    }
    catch (elle::Error const&e)
    {
      ELLE_TRACE(
        "mount options serialization error, assuming old version: %s", e);
    }
  }

  std::unique_ptr<elle::reactor::filesystem::FileSystem>
  Volume::run(std::shared_ptr<model::doughnut::Doughnut> dht,
              bool allow_root_creation,
              bool map_other_permissions
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
              , boost::optional<std::string> volname
#endif
#ifdef INFINIT_MACOSX
              , boost::optional<std::string> volicon
#endif
    )
  {
    ELLE_TRACE("mount options: %s", this->mount_options);
    auto mountpoint = boost::optional<bfs::path>{};
    if (this->mount_options.mountpoint)
      mountpoint = bfs::path(*this->mount_options.mountpoint);
    auto fs = std::make_unique<filesystem::FileSystem>(
      filesystem::model = dht,
      filesystem::allow_root_creation = allow_root_creation,
      filesystem::volume_name = this->name,
      filesystem::root_block_cache_dir = this->root_block_cache_dir(),
      filesystem::mountpoint = mountpoint,
      filesystem::map_other_permissions = map_other_permissions,
      filesystem::block_size = block_size);
    auto driver =
      std::make_unique<elle::reactor::filesystem::FileSystem>(std::move(fs), true);
    if (mountpoint)
    {
      auto fuse_options = std::vector<std::string>{
        "infinit-volume",
      };
      auto add =
        [&fuse_options] (std::string const& name,
                         std::string const& val = {})
        {
          elle::push_back(fuse_options,
                          "-o",
                          val.empty()
                          ? elle::sprintf("%s", name)
                          : elle::sprintf("%s=%s", name, val));
        };
      add("noatime");
      add("hard_remove");
      if (mount_options.readonly.value_or(false))
        add("ro");
#ifndef INFINIT_WINDOWS
      if (mount_options.fuse_options)
        for (auto const& opt: *mount_options.fuse_options)
          add(opt);
#endif
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
      add("volname", volname.value_or(this->name));
#endif
#ifdef INFINIT_MACOSX
      add("daemon_timeout", "600");
      if (volicon)
        add("volicon", *volicon);
#endif
      driver->mount(*mountpoint, fuse_options);
    }
    return driver;
  }

  bfs::path
  Volume::root_block_cache_dir() const
  {
    return xdg_state_home() / this->network / std::string(this->name);
  }

  void
  Volume::print(std::ostream& out) const
  {
    out << "Volume(" << this->name << ")";
  }
}
