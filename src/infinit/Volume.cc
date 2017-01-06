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
                 boost::optional<std::string> description)
    : descriptor::TemplatedBaseDescriptor<Volume>(std::move(name),
                                                  std::move(description))
    , network(std::move(network))
    , mount_options(mount_options)
    , default_permissions(std::move(default_permissions))
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
    try
    {
      s.serialize("mount_options", this->mount_options);
    }
    catch (elle::Error const&e)
    {
      ELLE_TRACE("mount_options serialization error, assuming old version: %s", e);
    }
  }

  std::unique_ptr<reactor::filesystem::FileSystem>
  Volume::run(std::shared_ptr<model::doughnut::Doughnut> dht,
              boost::optional<std::string> mountpoint_,
              boost::optional<bool> readonly,
              bool allow_root_creation,
              bool map_other_permissions
#if defined(INFINIT_MACOSX) || defined(INFINIT_WINDOWS)
              , boost::optional<std::string> volname_
#endif
#ifdef INFINIT_MACOSX
              , boost::optional<std::string> volicon_
#endif
    )
  {
#if defined(INFINIT_MACOSX) || defined(INFINIT_WINDOWS)
    if (!volname_)
      volname_ = this->name;
#endif
    {
      std::vector<std::string> opts;
      std::unordered_map<std::string, std::string> env;
      mount_options.to_commandline(opts, env);
      ELLE_TRACE("mount options: %s  environ %s", opts, env);
    }
    boost::optional<boost::filesystem::path> mountpoint;
    if (this->mount_options.mountpoint)
      mountpoint = boost::filesystem::path(this->mount_options.mountpoint.get());
    auto fs = elle::make_unique<filesystem::FileSystem>(
      infinit::filesystem::model = dht,
      infinit::filesystem::allow_root_creation = allow_root_creation,
      infinit::filesystem::volume_name = this->name,
      infinit::filesystem::root_block_cache_dir = this->root_block_cache_dir(),
      infinit::filesystem::mountpoint = mountpoint,
      infinit::filesystem::map_other_permissions = map_other_permissions);
    auto driver =
      elle::make_unique<reactor::filesystem::FileSystem>(std::move(fs), true);
    if (mountpoint)
    {
      std::vector<std::string> fuse_options = {
        "infinit-volume",
        "-o", "noatime",
        "-o", "hard_remove",
      };
      if (mount_options.readonly && mount_options.readonly.get())
      {
        fuse_options.push_back("-o");
        fuse_options.push_back("ro");
      }
#ifndef INFINIT_WINDOWS
      if (mount_options.fuse_options)
        for (auto const& opt: *mount_options.fuse_options)
        {
          fuse_options.push_back("-o");
          fuse_options.push_back(opt);
        }
#endif
#if defined(INFINIT_MACOSX) || defined(INFINIT_WINDOWS)
      auto add_option =
        [&fuse_options] (std::string const& opt_name,
                         std::string const& opt_val)
        {
          fuse_options.push_back("-o");
          fuse_options.push_back(elle::sprintf("%s=%s", opt_name, opt_val));
        };
      add_option("volname", volname_.get());
#endif
#ifdef INFINIT_MACOSX
      add_option("daemon_timeout", "600");
      if (volicon_)
        add_option("volicon", volicon_.get());
#endif
      driver->mount(mountpoint.get(), fuse_options);
    }
    return driver;
  }

  boost::filesystem::path
  Volume::root_block_cache_dir()
  {
    return xdg_state_home() / this->network / std::string(this->name);
  }

  void
  Volume::print(std::ostream& out) const
  {
    out << "Volume(" << this->name << ")";
  }
}
