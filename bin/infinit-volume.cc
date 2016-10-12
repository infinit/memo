// http://opensource.apple.com/source/mDNSResponder/mDNSResponder-576.30.4/mDNSPosix/PosixDaemon.c
#if __APPLE__
# define daemon yes_we_know_that_daemon_is_deprecated_in_os_x_10_5_thankyou
#endif

#include <elle/log.hh>
#include <elle/serialization/json.hh>

#ifndef INFINIT_WINDOWS
# include <reactor/network/unix-domain-socket.hh>
#endif

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/storage/Storage.hh>

#ifdef INFINIT_MACOSX
# include <reactor/network/reachability.hh>
# define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
# include <crash_reporting/gcc_fix.hh>
# include <CoreServices/CoreServices.h>
#endif

ELLE_LOG_COMPONENT("infinit-volume");

#include <main.hh>

#ifdef INFINIT_WINDOWS
# include <fcntl.h>
#endif

infinit::Infinit ifnt;

#if __APPLE__
# undef daemon
extern "C" int daemon(int, int);
#endif

using boost::program_options::variables_map;

static
std::string
volume_name(variables_map const& args, infinit::User const& owner)
{
  return ifnt.qualified_name(mandatory(args, "name", "volume name"), owner);
}

COMMAND(create)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  auto network = ifnt.network_get(mandatory(args, "network"), owner);
  auto default_permissions = optional(args, "default-permissions");
  infinit::MountOptions mo;
  mo.merge(args);
  if (default_permissions && *default_permissions!= "r"
      && *default_permissions!= "rw")
    throw elle::Error("default-permissions must be 'r' or 'rw'");
  infinit::Volume volume(name, network.name, mo, default_permissions);
  if (args.count("output"))
  {
    auto output = get_output(args);
    elle::serialization::json::SerializerOut s(*output, false);
    s.serialize_forward(volume);
  }
  else
  {
    bool root = flag(args, "create-root");
    bool discovery = flag(args, "register-service");
    if (root || discovery)
    {
      auto model = network.run(
        owner, mo, false, infinit::compatibility_version);
      if (discovery)
        model->service_add("volumes", name, volume);
      if (root)
      {
        auto fs = elle::make_unique<infinit::filesystem::FileSystem>(
          infinit::filesystem::model = std::move(model),
          infinit::filesystem::volume_name = name,
          infinit::filesystem::allow_root_creation = true);
        struct stat s;
        fs->path("/")->stat(&s);
      }
    }
    ifnt.volume_save(volume);
    report_created("volume", name);
  }
  if (option_push(args, {"push-volume"}))
    beyond_push("volume", name, volume, owner);
}

COMMAND(export_)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  auto output = get_output(args);
  auto volume = ifnt.volume_get(name);
  volume.mount_options.mountpoint.reset();
  {
    elle::serialization::json::SerializerOut s(*output, false);
    s.serialize_forward(volume);
  }
  report_exported(*output, "volume", volume.name);
}

COMMAND(import)
{
  auto input = get_input(args);
  elle::serialization::json::SerializerIn s(*input, false);
  infinit::Volume volume(s);
  volume.mount_options.mountpoint = optional(args, "mountpoint");
  ifnt.volume_save(volume);
  report_imported("volume", volume.name);
}

COMMAND(push)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  auto volume = ifnt.volume_get(name);
  // Don't push the mountpoint to beyond.
  volume.mount_options.mountpoint = boost::none;
  auto network = ifnt.network_get(volume.network, owner);
  auto owner_uid = infinit::User::uid(*network.dht()->owner);
  beyond_push("volume", name, volume, owner);
}

COMMAND(pull)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  beyond_delete("volume", name, owner, false, flag(args, "purge"));
}

COMMAND(delete_)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  auto path = ifnt._volume_path(name);
  auto volume = ifnt.volume_get(name);
  bool purge = flag(args, "purge");
  bool pull = flag(args, "pull");
  if (purge)
  {
    for (auto const& drive: ifnt.drives_for_volume(name))
    {
      auto drive_path = ifnt._drive_path(drive);
      if (boost::filesystem::remove(drive_path))
        report_action("deleted", "drive", drive, std::string("locally"));
    }
  }
  if (pull)
    beyond_delete("volume", name, owner, true, purge);
  boost::filesystem::remove_all(volume.root_block_cache_dir());
  if (boost::filesystem::remove(path))
    report_action("deleted", "volume", name, std::string("locally"));
  else
  {
    throw elle::Error(
      elle::sprintf("File for volume could not be deleted: %s", path));
  }
}

COMMAND(fetch)
{
  auto owner = self_user(ifnt, args);
  auto network_name_ = optional(args, "network");
  if (optional(args, "name"))
  {
    auto name = volume_name(args, owner);
    auto desc = beyond_fetch<infinit::Volume>("volume", name);
    ifnt.volume_save(std::move(desc));
  }
  else if (network_name_) // Fetch all networks for network.
  {
    std::string network_name = ifnt.qualified_name(network_name_.get(), owner);
    auto res = beyond_fetch<
      std::unordered_map<std::string, std::vector<infinit::Volume>>>(
        elle::sprintf("networks/%s/volumes", network_name),
        "volumes for network",
        network_name);
    for (auto const& volume: res["volumes"])
      ifnt.volume_save(std::move(volume));
  }
  else // Fetch all networks for owner.
  {
    auto res = beyond_fetch<
      std::unordered_map<std::string, std::vector<infinit::Volume>>>(
        elle::sprintf("users/%s/volumes", owner.name),
        "volumes for user",
        owner.name,
        owner);
    for (auto const& volume: res["volumes"])
    {
      try
      {
        ifnt.volume_save(std::move(volume));
      }
      catch (ResourceAlreadyFetched const& error)
      {
      }
    }
  }
}

#ifdef INFINIT_MACOSX
static
void
add_path_to_finder_sidebar(std::string const& path)
{
  ELLE_DUMP("add to sidebar: %s", path);
  LSSharedFileListRef favorite_items =
    LSSharedFileListCreate(NULL, kLSSharedFileListFavoriteItems, NULL);
  if (!favorite_items)
    return;
  CFStringRef path_str = CFStringCreateWithCString(
    kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
  CFArrayRef items_array = LSSharedFileListCopySnapshot(favorite_items, NULL);
  CFIndex count = CFArrayGetCount(items_array);
  LSSharedFileListItemRef item_ref;
  bool in_list = false;
  for (CFIndex i = 0; i < count; i++)
  {
    item_ref =
      (LSSharedFileListItemRef)CFArrayGetValueAtIndex(items_array, i);
    CFURLRef item_url;
    OSStatus err = LSSharedFileListItemResolve(
      item_ref,
      kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes,
      &item_url,
      NULL);
    if (err == noErr && item_url)
    {
      CFStringRef item_path = CFURLCopyPath(item_url);
      if (item_path)
      {
        if (CFStringHasPrefix(item_path, path_str))
        {
          ELLE_DEBUG("already in sidebar favorites: %s", path);
          in_list = true;
        }
        CFRelease(item_path);
      }
      CFRelease(item_url);
    }
  }
  if (items_array)
    CFRelease(items_array);
  if (path_str)
    CFRelease(path_str);
  if (!in_list)
  {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault,
      reinterpret_cast<const unsigned char*>(path.data()),
      path.size(),
      true);
    LSSharedFileListItemRef item = LSSharedFileListInsertItemURL(
      favorite_items, kLSSharedFileListItemLast, NULL, NULL, url, NULL, NULL);
    ELLE_DEBUG("added to sidebar favorites: %s", path);
    if (url)
      CFRelease(url);
    if (item)
      CFRelease(item);
  }
  if (favorite_items)
    CFRelease(favorite_items);
}

static
void
remove_path_from_finder_sidebar(std::string const& path)
{
  ELLE_DUMP("remove from sidebar: %s", path);
  LSSharedFileListRef favorite_items =
    LSSharedFileListCreate(NULL, kLSSharedFileListFavoriteItems, NULL);
  if (!favorite_items)
    return;
  CFStringRef path_str = CFStringCreateWithCString(
    kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
  CFArrayRef items_array = LSSharedFileListCopySnapshot(favorite_items, NULL);
  CFIndex count = CFArrayGetCount(items_array);
  LSSharedFileListItemRef item_ref;
  for (CFIndex i = 0; i < count; i++)
  {
    item_ref =
      (LSSharedFileListItemRef)CFArrayGetValueAtIndex(items_array, i);
    CFURLRef item_url;
    OSStatus err = LSSharedFileListItemResolve(
      item_ref,
      kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes,
      &item_url,
      NULL);
    if (err == noErr && item_url)
    {
      CFStringRef item_path = CFURLCopyPath(item_url);
      if (item_path)
      {
        if (CFStringHasPrefix(item_path, path_str))
        {
          LSSharedFileListItemRemove(favorite_items, item_ref);
          ELLE_DEBUG("found and removed item from sidebar: %s", path);
        }
        CFRelease(item_path);
      }
      CFRelease(item_url);
    }
  }
  if (items_array)
    CFRelease(items_array);
  if (path_str)
    CFRelease(path_str);
  if (favorite_items)
    CFRelease(favorite_items);
}
#endif

COMMAND(run)
{
  auto self = self_user(ifnt, args);
  auto name = volume_name(args, self);
  auto volume = ifnt.volume_get(name);
  volume.mount_options.merge(args);
  auto& mo = volume.mount_options;
#ifdef INFINIT_MACOSX
  if (mo.mountpoint && !flag(args, option_disable_mac_utf8))
  {
    if (!mo.fuse_options)
      mo.fuse_options = std::vector<std::string>();
    mo.fuse_options.get().push_back("modules=iconv,from_code=UTF-8,to_code=UTF-8-MAC");
  }
#endif
  bool created_mountpoint = false;
  if (mo.mountpoint)
  {
#ifdef INFINIT_WINDOWS
    if (mo.mountpoint.get().size() == 2 && mo.mountpoint.get()[1] == ':')
      ;
    else
#elif defined(INFINIT_MACOSX)
    // Do not try to create folder in /Volumes.
    auto mount_path = boost::filesystem::path(mo.mountpoint.get());
    auto mount_parent = mount_path.parent_path().string();
    boost::algorithm::to_lower(mount_parent);
    if (mount_parent.find("/volumes") == 0)
      ;
    else
#endif
    try
    {
      if (boost::filesystem::exists(mo.mountpoint.get()))
      {
        if (!boost::filesystem::is_directory(mo.mountpoint.get()))
          throw (elle::Error("mountpoint is not a directory"));
        if (!boost::filesystem::is_empty(mo.mountpoint.get()))
          throw elle::Error("mountpoint is not empty");
      }
      created_mountpoint =
        boost::filesystem::create_directories(mo.mountpoint.get());
    }
    catch (boost::filesystem::filesystem_error const& e)
    {
      throw elle::Error(elle::sprintf("unable to access mountpoint: %s",
                                      e.what()));
    }
  }
  if (mo.fuse_options)
  {
    if (!mo.mountpoint)
      throw CommandLineError("FUSE options require the volume to be mounted");
  }
  auto network = ifnt.network_get(volume.network, self);
  network.ensure_allowed(self, "run", "volume");
  ELLE_TRACE("run network");
#ifndef INFINIT_WINDOWS
  if (flag(args, "daemon"))
    if (daemon(0, 1))
      perror("daemon:");
#endif
  report_action("running", "network", network.name);
  auto compatibility = optional(args, "compatibility-version");
  auto port = optional<int>(args, option_port);
  auto model = network.run(
    self, mo, true, infinit::compatibility_version, port);
  // Only push if we have are contributing storage.
  bool push = mo.push && model->local();
  boost::optional<infinit::model::Endpoint> local_endpoint;
  if (model->local())
  {
    local_endpoint = model->local()->server_endpoint();
    if (auto port_file = optional(args, option_port_file))
      infinit::port_to_file(local_endpoint.get().port(), port_file.get());
    if (auto endpoint_file = optional(args, option_endpoint_file))
    {
      infinit::endpoints_to_file(model->local()->server_endpoints(),
                                 endpoint_file.get());
    }
  }
  auto run = [&]
  {
    reactor::Thread::unique_ptr stat_thread;
    if (push)
      stat_thread = make_stat_update_thread(self, network, *model);
    ELLE_TRACE_SCOPE("run volume");
    report_action("running", "volume", volume.name);
    auto fs = volume.run(std::move(model),
                         mo.mountpoint,
                         mo.readonly,
                         flag(args, "allow-root-creation")
#if defined(INFINIT_MACOSX) || defined(INFINIT_WINDOWS)
                         , optional(args, "mount-name")
#endif
#ifdef INFINIT_MACOSX
                         , optional(args, "mount-icon")
#endif
                         );
    boost::signals2::scoped_connection killer = killed.connect(
      [&, count = std::make_shared<int>(0)] ()
      {
        if (*count == 0)
          ++*count;
        else if (*count == 1)
        {
          ++*count;
          ELLE_LOG("already shutting down gracefully, "
                   "repeat to force filesystem operations termination");
        }
        else if (*count == 2)
        {
          ++*count;
          ELLE_LOG("force termination");
          fs->kill();
        }
        else
          ELLE_LOG(
            "already forcing termination as hard as possible");
      });
    // Experimental: poll root on mount to trigger caching.
#   if 0
    boost::optional<std::thread> root_poller;
    if (mo.mountpoint && mo.cache && mo.cache.get())
      root_poller.emplace(
        [root = mo.mountpoint.get()]
        {
          try
          {
            boost::filesystem::status(root);
            for (auto it = boost::filesystem::directory_iterator(root);
                 it != boost::filesystem::directory_iterator();
                 ++it)
              ;
          }
          catch (boost::filesystem::filesystem_error const& e)
          {
            ELLE_WARN("error polling root: %s", e);
          }
        });
    elle::SafeFinally root_poller_join(
      [&]
      {
        if (root_poller)
          root_poller->join();
      });
#   endif
    if (volume.default_permissions && !volume.default_permissions->empty())
    {
      auto ops =
        dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get());
      ops->on_root_block_create.connect([&] {
        ELLE_DEBUG("root_block hook triggered");
        auto path = fs->path("/");
        int mode = 0700;
        if (*volume.default_permissions == "rw")
          mode |= 06;
        else if (*volume.default_permissions == "r")
          mode |= 04;
        else
        {
          ELLE_WARN("Unexpected default permissions %s",
                    *volume.default_permissions);
          return;
        }
        path->chmod(mode);
        path->setxattr("infinit.auth.inherit", "1", 0);
      });
    }
#ifdef INFINIT_MACOSX
    auto add_to_sidebar = flag(args, "finder-sidebar");
    if (add_to_sidebar && mo.mountpoint)
    {
      auto mountpoint = mo.mountpoint;
      reactor::background([mountpoint]
        {
          add_path_to_finder_sidebar(mountpoint.get());
        });
    }
    std::unique_ptr<reactor::network::Reachability> reachability;
#endif
    elle::SafeFinally unmount([&]
    {
#ifdef INFINIT_MACOSX
      if (reachability)
        reachability->stop();
      if (add_to_sidebar && mo.mountpoint)
      {
        auto mountpoint = mo.mountpoint;
        reactor::background([mountpoint]
          {
            remove_path_from_finder_sidebar(mountpoint.get());
          });
      }
#endif
      ELLE_TRACE("unmounting")
        fs->unmount();
      if (created_mountpoint)
      {
        try
        {
          boost::filesystem::remove(mo.mountpoint.get());
        }
        catch (boost::filesystem::filesystem_error const&)
        {}
      }
    });
#ifdef INFINIT_MACOSX
    if (elle::os::getenv("INFINIT_LOG_REACHABILITY", "") != "0")
    {
      reachability.reset(new reactor::network::Reachability(
        {},
        [&] (reactor::network::Reachability::NetworkStatus status)
        {
          using NetworkStatus = reactor::network::Reachability::NetworkStatus;
          if (status == NetworkStatus::Unreachable)
            ELLE_LOG("lost network connection");
          else
            ELLE_LOG("got network connection");
        },
        true));
      reachability->start();
    }
#endif
    if (script_mode)
    {
      auto input = infinit::commands_input(args);
      std::unordered_map<std::string,
        std::unique_ptr<reactor::filesystem::Handle>> handles;
      while (true)
      {
        std::string op;
        std::string pathname;
        std::string handlename;
        try
        {
          auto json =
            boost::any_cast<elle::json::Object>(elle::json::read(*input));
          ELLE_TRACE("got command: %s", json);
          elle::serialization::json::SerializerIn command(json, false);
          op = command.deserialize<std::string>("operation");
          std::shared_ptr<reactor::filesystem::Path> path;
          try
          {
            pathname = command.deserialize<std::string>("path");
            path = fs->path(pathname);
          }
          catch (...)
          {}
          try
          {
            handlename = command.deserialize<std::string>("handle");
          }
          catch (...)
          {}
          auto require_path = [&]
            {
              if (!path)
                throw reactor::filesystem::Error(
                  ENOENT,
                  elle::sprintf("no such file or directory: %s", pathname));
            };
          if (op == "list_directory")
          {
            require_path();
            std::vector<std::string> entries;
            path->list_directory(
              [&] (std::string const& path, struct stat*)
              {
                entries.push_back(path);
              });
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("entries", entries);
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("path", pathname);
            continue;
          }
          else if (op == "mkdir")
          {
            require_path();
            path->mkdir(0777);
          }
          else if (op == "stat")
          {
            require_path();
            struct stat st;
            path->stat(&st);
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("path", pathname);
            response.serialize("st_dev"    , st.st_dev            );
            response.serialize("st_ino"    , st.st_ino            );
            response.serialize("st_mode"   , st.st_mode           );
            response.serialize("st_nlink"  , st.st_nlink          );
            response.serialize("st_uid"    , st.st_uid            );
            response.serialize("st_gid"    , st.st_gid            );
            response.serialize("st_rdev"   , st.st_rdev           );
            response.serialize("st_size"   , uint64_t(st.st_size) );
#ifndef INFINIT_WINDOWS
            response.serialize("st_blksize", int64_t(st.st_blksize));
            response.serialize("st_blocks" , st.st_blocks);
#endif
            response.serialize("st_atime"  , uint64_t(st.st_atime));
            response.serialize("st_mtime"  , uint64_t(st.st_mtime));
            response.serialize("st_ctime"  , uint64_t(st.st_ctime));
            continue;
          }
          else if (op == "setxattr")
          {
            require_path();
            auto name = command.deserialize<std::string>("name");
            auto value = command.deserialize<std::string>("value");
            path->setxattr(name, value, 0);
          }
          else if (op == "getxattr")
          {
            require_path();
            auto name = command.deserialize<std::string>("name");
            auto value = path->getxattr(name);
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("value", value);
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("path", pathname);
            continue;
          }
          else if (op == "listxattr")
          {
            require_path();
            auto attrs = path->listxattr();
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("entries", attrs);
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("path", pathname);
            continue;
          }
          else if (op == "removexattr")
          {
            require_path();
            auto name = command.deserialize<std::string>("name");
            path->removexattr(name);
          }
          else if (op == "link")
          {
            require_path();
            auto target = command.deserialize<std::string>("target");
            path->link(target);
          }
          else if (op == "symlink")
          {
            require_path();
            auto target = command.deserialize<std::string>("target");
            path->symlink(target);
          }
          else if (op == "readlink")
          {
            require_path();
            auto res = path->readlink();
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("target", res.string());
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("path", pathname);
            continue;
          }
          else if (op == "rename")
          {
            require_path();
            auto target = command.deserialize<std::string>("target");
            path->rename(target);
          }
          else if (op == "truncate")
          {
            require_path();
            auto sz = command.deserialize<uint64_t>("size");
            path->truncate(sz);
          }
          else if (op == "utimens")
          {
            require_path();
            auto last_access = command.deserialize<uint64_t>("access");
            auto last_change = command.deserialize<uint64_t>("change");
            timespec ts[2];
            ts[0].tv_sec = last_access / 1000000000ULL;
            ts[0].tv_nsec = last_access % 1000000000Ull;
            ts[1].tv_sec = last_change / 1000000000ULL;
            ts[1].tv_nsec = last_change % 1000000000Ull;
            path->utimens(ts);
          }
          else if (op == "statfs")
          {
            require_path();
            struct statvfs sv;
            path->statfs(&sv);
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("path", pathname);
            response.serialize("f_bsize"  , uint64_t(sv.f_bsize)  );
            response.serialize("f_frsize" , uint64_t(sv.f_frsize) );
            response.serialize("f_blocks" , uint64_t(sv.f_blocks) );
            response.serialize("f_bfree"  , uint64_t(sv.f_bfree)  );
            response.serialize("f_bavail" , uint64_t(sv.f_bavail) );
            response.serialize("f_files"  , uint64_t(sv.f_files)  );
            response.serialize("f_ffree"  , uint64_t(sv.f_ffree)  );
            response.serialize("f_favail" , uint64_t(sv.f_favail) );
            response.serialize("f_fsid"   , uint64_t(sv.f_fsid)   );
            response.serialize("f_flag"   , uint64_t(sv.f_flag)   );
            response.serialize("f_namemax", uint64_t(sv.f_namemax));
            continue;
          }
          else if (op == "chown")
          {
            require_path();
            auto uid = command.deserialize<int>("uid");
            auto gid = command.deserialize<int>("gid");
            path->chown(uid, gid);
          }
          else if (op == "chmod")
          {
            require_path();
            auto mode = command.deserialize<int>("mode");
            path->chmod(mode);
          }
          else if (op == "write_file")
          {
            require_path();
            auto content = command.deserialize<std::string>("content");
            auto handle = path->create(O_TRUNC|O_CREAT, 0100666);
            handle->write(elle::WeakBuffer(elle::unconst(content.data()),
                                           content.size()),
                          content.size(), 0);
            handle->close();
          }
          else if (op == "read_file")
          {
            require_path();
            struct stat st;
            path->stat(&st);
            auto handle = path->open(O_RDONLY, 0666);
            std::string content(st.st_size, char(0));
            handle->read(elle::WeakBuffer(elle::unconst(content.data()),
                                          content.size()),
                         st.st_size, 0);
            handle->close();
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("content", content);
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("path", pathname);
            continue;
          }
          else if (op == "unlink")
          {
            require_path();
            path->unlink();
          }
          else if (op == "rmdir")
          {
            require_path();
            path->rmdir();
          }
          else if (op == "create")
          {
            require_path();
            int flags = command.deserialize<int>("flags");
            int mode = command.deserialize<int>("mode");
            auto handle = path->create(flags, mode);
            handles[handlename] = std::move(handle);
            handlename = "";
          }
          else if (op == "open")
          {
            require_path();
            int flags = command.deserialize<int>("flags");
            int mode = command.deserialize<int>("mode");
            auto handle = path->open(flags, mode);
            handles[handlename] = std::move(handle);
            handlename = "";
          }
          else if (op == "close")
          {
            handles.at(handlename)->close();
          }
          else if (op == "dispose")
          {
            handles.erase(handlename);
          }
          else if (op == "read")
          {
            uint64_t offset = command.deserialize<uint64_t>("offset");
            uint64_t size = command.deserialize<uint64_t>("size");
            elle::Buffer buf;
            buf.size(size);
            int nread = handles.at(handlename)->read(
              elle::WeakBuffer(buf.contents(), buf.size()),
              size, offset);
            buf.size(nread);
            elle::serialization::json::SerializerOut response(std::cout);
            response.serialize("content", buf);
            response.serialize("success", true);
            response.serialize("operation", op);
            response.serialize("handle", handlename);
            continue;
          }
          else if (op == "write")
          {
            uint64_t offset = command.deserialize<uint64_t>("offset");
            uint64_t size = command.deserialize<uint64_t>("size");
            elle::Buffer content = command.deserialize<elle::Buffer>("content");
            handles.at(handlename)->write(
              elle::WeakBuffer(content.mutable_contents(), content.size()),
              size, offset);
          }
          else if (op == "ftruncate")
          {
            uint64_t size = command.deserialize<uint64_t>("size");
            handles.at(handlename)->ftruncate(size);
          }
          else if (op == "fsync")
          {
            auto datasync = command.deserialize<int>("datasync");
            handles.at(handlename)->fsync(datasync);
          }
          else if (op == "fsyncdir")
          {
            auto datasync = command.deserialize<int>("datasync");
            handles.at(handlename)->fsyncdir(datasync);
          }
          else
            throw elle::Error(elle::sprintf("operation %s does not exist", op));
          elle::serialization::json::SerializerOut response(std::cout);
          response.serialize("success", true);
          response.serialize("operation", op);
          if (!handlename.empty())
            response.serialize("handle", handlename);
          if (!pathname.empty())
            response.serialize("path", pathname);
        }
        catch (reactor::filesystem::Error const& e)
        {
          elle::serialization::json::SerializerOut response(std::cout);
          response.serialize("success", false);
          response.serialize("message", e.what());
          response.serialize("code", e.error_code());
          response.serialize("operation", op);
          if (!handlename.empty())
            response.serialize("handle", handlename);
          if (!pathname.empty())
            response.serialize("path", pathname);
        }
        catch (elle::Error const& e)
        {
          if (input->eof())
            return;
          ELLE_LOG("bronk on op %s: %s", op, e);
          elle::serialization::json::SerializerOut response(std::cout);
          response.serialize("success", false);
          response.serialize("message", e.what());
          response.serialize("operation", op);
          if (!handlename.empty())
            response.serialize("handle", handlename);
          if (!pathname.empty())
            response.serialize("path", pathname);
        }
      }
    }
    else
    {
      ELLE_TRACE("wait filesystem");
      reactor::wait(*fs);
    }
  };
  if (local_endpoint && push)
  {
    auto advertise = optional<std::vector<std::string>>(args, "advertise-host");
    elle::With<InterfacePublisher>(
      network, self, model->id(), local_endpoint.get().port(), advertise) << [&]
    {
      run();
    };
  }
  else
    run();
}

COMMAND(mount)
{
  auto mountpoint = optional(args, "mountpoint");
  if (!mountpoint)
  {
    auto self = self_user(ifnt, args);
    auto name = volume_name(args, self);
    auto volume = ifnt.volume_get(name);
    if (!volume.mount_options.mountpoint)
    {
      mandatory(args, "mountpoint", "mountpoint");
    }
  }
  run(args, killed);
}

COMMAND(list)
{
  if (script_mode)
  {
    elle::json::Array l;
    for (auto const& volume: ifnt.volumes_get())
    {
      elle::json::Object o;
      o["name"] = std::string(volume.name);
      o["network"] = volume.network;
      if (volume.mount_options.mountpoint)
        o["mountpoint"] = volume.mount_options.mountpoint.get();
      l.push_back(std::move(o));
    }
    elle::json::write(std::cout, l);
  }
  else
    for (auto const& volume: ifnt.volumes_get())
    {
      std::cout << volume.name << ": network " << volume.network;
      if (volume.mount_options.mountpoint)
        std::cout << " on " << volume.mount_options.mountpoint.get();
      std::cout << std::endl;
    }
}

COMMAND(update)
{
  auto self = self_user(ifnt, args);
  auto name = volume_name(args, self);
  auto volume = ifnt.volume_get(name);
  volume.mount_options.merge(args);
  ifnt.volume_save(volume, true);
  if (option_push(args, {"push-volume"}))
    beyond_push("volume", name, volume, self);
}

#ifndef INFINIT_WINDOWS
COMMAND(start)
{
  auto self = self_user(ifnt, args);
  auto name = volume_name(args, self);
  infinit::MountOptions mo;
  mo.merge(args);
  reactor::network::UnixDomainSocket sock(daemon_sock_path());
  std::stringstream ss;
  {
    elle::serialization::json::SerializerOut cmd(ss, false);
    cmd.serialize("operation", "volume-start");
    cmd.serialize("volume", name);
    cmd.serialize("options", mo);
  }
  sock.write(elle::ConstWeakBuffer(ss.str().data(), ss.str().size()));
  auto reply = sock.read_until("\n").string();
  std::stringstream replystream(reply);
  auto json = elle::json::read(replystream);
  auto jsono = boost::any_cast<elle::json::Object>(json);
  if (boost::any_cast<std::string>(jsono.at("result")) != "Ok")
  {
    std::cout << elle::json::pretty_print(json) << std::endl;
    throw elle::Exit(1);
  }
  std::cout << "Ok" << std::endl;
}

COMMAND(stop)
{
  auto self = self_user(ifnt, args);
  auto name = volume_name(args, self);
  reactor::network::UnixDomainSocket sock(daemon_sock_path());
  std::stringstream ss;
  {
    elle::serialization::json::SerializerOut cmd(ss, false);
    cmd.serialize("operation", "volume-stop");
    cmd.serialize("volume", name);
  }
  sock.write(elle::ConstWeakBuffer(ss.str().data(), ss.str().size()));
  auto reply = sock.read_until("\n").string();
  std::stringstream replystream(reply);
  auto json = elle::json::read(replystream);
  auto jsono = boost::any_cast<elle::json::Object>(json);
  if (boost::any_cast<std::string>(jsono.at("result")) != "Ok")
  {
    std::cout << elle::json::pretty_print(json) << std::endl;
    throw elle::Exit(1);
  }
  std::cout << "Ok" << std::endl;
}

COMMAND(status)
{
  auto self = self_user(ifnt, args);
  auto name = volume_name(args, self);
  reactor::network::UnixDomainSocket sock(daemon_sock_path());
  std::stringstream ss;
  {
    elle::serialization::json::SerializerOut cmd(ss, false);
    cmd.serialize("operation", "volume-status");
    cmd.serialize("volume", name);
  }
  sock.write(elle::ConstWeakBuffer(ss.str().data(), ss.str().size()));
  auto reply = sock.read_until("\n").string();
  std::stringstream replystream(reply);
  auto json = elle::json::read(replystream);
  auto jsono = boost::any_cast<elle::json::Object>(json);
  if (boost::any_cast<std::string>(jsono.at("result")) != "Ok"
    || !boost::any_cast<bool>(jsono.at("live")))
  {
    std::cout << elle::json::pretty_print(json) << std::endl;
    throw elle::Exit(1);
  }
  std::cout << "Ok" << std::endl;
}
#endif

enum class RunMode
{
  create,
  run,
  update,
};

std::vector<Mode::OptionDescription>
run_options(RunMode mode)
{
  using boost::program_options::value;
#define BOOL_IMPLICIT \
  boost::program_options::value<bool>()->implicit_value(true, "true")
  std::vector<Mode::OptionDescription> res;
  auto add_option = [&res] (Mode::OptionDescription const& opt) {
    res.push_back(opt);
  };
  auto add_options = [&res] (std::vector<Mode::OptionDescription> const& opts) {
    for (auto const& opt: opts)
      res.push_back(opt);
  };
  add_option({ "name", value<std::string>(), "volume name" });
  if (mode == RunMode::create)
  {
    add_options({
      { "create-root,R", BOOL_IMPLICIT, "create root directory"},
      { "network,N", value<std::string>(), "underlying network to use" },
      { "push-volume", BOOL_IMPLICIT,
        elle::sprintf("push the volume to %s", beyond(true)) },
      option_output("volume"),
      { "default-permissions,d", value<std::string>(),
        "default permissions (optional: r,rw)"},
      { "register-service,r", BOOL_IMPLICIT, "register volume in the network"},
    });
  }
  add_options({
    { "allow-root-creation", BOOL_IMPLICIT,
      "create the filesystem root if not found" },
    { "mountpoint,m", value<std::string>(), "where to mount the filesystem" },
    { "readonly", BOOL_IMPLICIT, "mount as readonly" },
#if defined(INFINIT_MACOSX) || defined(INFINIT_WINDOWS)
    { "mount-name", value<std::string>(), "name of mounted volume" },
#endif
#ifdef INFINIT_MACOSX
    { "mount-icon", value<std::string>(),
      "path to an icon for mounted volume" },
    { "finder-sidebar", BOOL_IMPLICIT, "show volume in Finder sidebar" },
#endif
    { "async", BOOL_IMPLICIT, "use asynchronous write operations" },
#ifndef INFINIT_WINDOWS
    { "daemon,d", BOOL_IMPLICIT, "run as a background daemon" },
    { "fuse-option", value<std::vector<std::string>>()->multitoken(),
      "option to pass directly to FUSE" },
#endif
    option_cache,
    option_cache_ram_size,
    option_cache_ram_ttl,
    option_cache_ram_invalidation,
    option_cache_disk_size,
    { "fetch-endpoints", BOOL_IMPLICIT,
      elle::sprintf("fetch endpoints from %s", beyond(true)) },
    { "fetch,f", BOOL_IMPLICIT, "alias for --fetch-endpoints" },
    { "peer", value<std::vector<std::string>>()->multitoken(),
      "peer address or file with list of peer addresses (host:port)" },
    { "push-endpoints", BOOL_IMPLICIT,
      elle::sprintf("push endpoints to %s", beyond(true)) },
  });
  if (mode == RunMode::create)
    add_option(
      { "push,p", BOOL_IMPLICIT, "alias for --push-endpoints --push-volume" });
  if (mode == RunMode::run || mode == RunMode::update)
    add_option({ "push,p", BOOL_IMPLICIT, "alias for --push-endpoints" });
  add_options({
    { "publish", BOOL_IMPLICIT,
      "alias for --fetch-endpoints --push-endpoints" },
    { "advertise-host", value<std::vector<std::string>>()->multitoken(),
      "advertise extra endpoint using given host"
    },
    option_endpoint_file,
    option_port_file,
    option_port,
    option_input("commands"),
  });
  if (mode == RunMode::update)
    add_option(
      { "user,u", value<std::string>(), "force mounting user to USER" });
#undef BOOL_IMPLICIT
  return res;
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  std::vector<Mode::OptionDescription> options_run_mount_hidden = {
#ifdef INFINIT_MACOSX
    option_disable_mac_utf8,
#endif
  };
  Modes modes {
    {
      "create",
      "Create a volume",
      &create,
      "--name VOLUME --network NETWORK [--mountpoint PATH]",
      run_options(RunMode::create),
    },
    {
      "export",
      "Export a volume for someone else to import",
      &export_,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to export" },
        option_output("volume"),
      },
    },
    {
      "fetch",
      elle::sprintf("Fetch a volume from %s", beyond(true)),
      &fetch,
      "",
      {
        { "name,n", value<std::string>(), "volume to fetch (optional)" },
        { "network", value<std::string>(),
          "network to fetch all volumes for (optional)" },
      },
    },
    {
      "import",
      "Import a volume",
      &import,
      "",
      {
        option_input("volume"),
        { "mountpoint", value<std::string>(),
          "default location to mount the volume (optional)" },
      },
    },
    {
      "push",
      elle::sprintf("Push a volume to %s", beyond(true)),
      &push,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to push" },
      },
    },
    {
      "run",
      "Run a volume",
      &run,
      "--name VOLUME [--mountpoint PATH]",
      run_options(RunMode::run),
      {},
      options_run_mount_hidden,
    },
    {
      "mount",
      "Mount a volume",
      &mount,
      "--name VOLUME [--mountpoint PATH]",
      run_options(RunMode::run),
      {},
      options_run_mount_hidden,
    },
    {
      "delete",
      "Delete a volume locally",
      &delete_,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to delete" },
        { "pull", bool_switch(),
          elle::sprintf("pull the volume if it is on %s", beyond(true)) },
        { "purge", bool_switch(), "remove objects that depend on the volume" },
      },
    },
    {
      "pull",
      elle::sprintf("Remove a volume from %s", beyond(true)),
      &pull,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to remove" },
        { "purge", bool_switch(), "remove objects that depend on the volume" },
      },
    },
    {
      "list",
      "List volumes",
      &list,
      {},
    },
    {
      "update",
      "Update a volume with default run options",
      &update,
      "--name VOLUME",
      run_options(RunMode::update),
    },
#ifndef INFINIT_WINDOWS
    {
      "start",
      "Start a volume through the daemon.",
      &start,
      "--name VOLUME [--mountpoint PATH]",
      run_options(RunMode::run),
    },
    {
      "stop",
      "Stop a volume",
      &stop,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to remove" },
      },
    },
    {
      "status",
      "Get volume status",
      &status,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to query" },
      },
    },
#endif
  };
  return infinit::main("Infinit volume management utility", modes, argc, argv);
}
