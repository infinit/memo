#include <infinit/Volume.hh>
#include <infinit/filesystem/filesystem.hh>
#include <infinit/utility.hh>

ELLE_LOG_COMPONENT("infinit");

namespace infinit
{
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
              boost::optional<std::string> mountpoint_, // FIXME: unused.
              boost::optional<bool> readonly,
              bool allow_root_creation,
              bool map_other_permissions
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
              , boost::optional<std::string> volname_
#endif
#ifdef INFINIT_MACOSX
              , boost::optional<std::string> volicon_
#endif
    )
  {
    {
      auto opts = std::vector<std::string>{};
      auto env = std::unordered_map<std::string, std::string>{};
      mount_options.to_commandline(opts, env);
      ELLE_TRACE("mount options: %s  environ %s", opts, env);
    }
    auto mountpoint = boost::optional<boost::filesystem::path>{};
    if (this->mount_options.mountpoint)
      mountpoint = boost::filesystem::path(this->mount_options.mountpoint.get());
    auto fs = std::make_unique<filesystem::FileSystem>(
      infinit::filesystem::model = dht,
      infinit::filesystem::allow_root_creation = allow_root_creation,
      infinit::filesystem::volume_name = this->name,
      infinit::filesystem::root_block_cache_dir = this->root_block_cache_dir(),
      infinit::filesystem::mountpoint = mountpoint,
      infinit::filesystem::map_other_permissions = map_other_permissions,
      infinit::filesystem::block_size = block_size);
    auto driver =
      std::make_unique<elle::reactor::filesystem::FileSystem>(std::move(fs), true);
    if (mountpoint)
    {
      auto fuse_options = std::vector<std::string>{
        "infinit-volume",
        "-o", "noatime",
        "-o", "hard_remove",
      };
      if (mount_options.readonly && mount_options.readonly.get())
      {
        fuse_options.emplace_back("-o");
        fuse_options.emplace_back("ro");
      }
#ifndef INFINIT_WINDOWS
      if (mount_options.fuse_options)
        for (auto const& opt: *mount_options.fuse_options)
        {
          fuse_options.emplace_back("-o");
          fuse_options.emplace_back(opt);
        }
#endif
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
      auto add_option =
        [&fuse_options] (std::string const& opt_name,
                         std::string const& opt_val)
        {
          fuse_options.emplace_back("-o");
          fuse_options.emplace_back(elle::sprintf("%s=%s", opt_name, opt_val));
        };
      add_option("volname", volname_.value_or(this->name));
#endif
#ifdef INFINIT_MACOSX
      add_option("daemon_timeout", "600");
      if (volicon_)
        add_option("volicon", *volicon_);
#endif
      driver->mount(*mountpoint, fuse_options);
    }
    return driver;
  }

  boost::filesystem::path
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
