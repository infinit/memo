#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <reactor/FDStream.hh>

#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/storage/Storage.hh>

#ifdef INFINIT_MACOSX
# if defined(__GNUC__) && !defined(__clang__)
#  undef __OSX_AVAILABLE_STARTING
#  define __OSX_AVAILABLE_STARTING(A, B)
# endif
# include <CoreServices/CoreServices.h>
#endif

ELLE_LOG_COMPONENT("infinit-volume");

#include <main.hh>

#ifdef INFINIT_WINDOWS
#include <fcntl.h>
#endif

using namespace boost::program_options;
options_description mode_options("Modes");

infinit::Infinit ifnt;

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
  auto mountpoint = optional(args, "mountpoint");
  auto network = ifnt.network_get(mandatory(args, "network"), owner);
  std::vector<std::string> hosts;
  infinit::overlay::NodeEndpoints eps;
  if (args.count("peer"))
  {
    hosts = args["peer"].as<std::vector<std::string>>();
    for (auto const& h: hosts)
      eps[elle::UUID()].push_back(h);
  }
  infinit::Volume volume(name, mountpoint, network.name);
  if (args.count("output"))
  {
    auto output = get_output(args);
    elle::serialization::json::SerializerOut s(*output, false);
    s.serialize_forward(volume);
  }
  else
  {
    ifnt.volume_save(volume);
    report_created("volume", name);
  }
  if (aliased_flag(args, {"push-volume", "push"}))
    beyond_push("volume", name, volume, owner);
}

COMMAND(export_)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  auto output = get_output(args);
  auto volume = ifnt.volume_get(name);
  volume.mountpoint.reset();
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
  volume.mountpoint = optional(args, "mountpoint");
  ifnt.volume_save(volume);
  report_imported("volume", volume.name);
}

COMMAND(push)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  auto volume = ifnt.volume_get(name);
  // Don't push the mountpoint to beyond.
  volume.mountpoint = boost::none;
  auto network = ifnt.network_get(volume.network, owner);
  auto owner_uid = infinit::User::uid(network.dht()->owner);
  beyond_push("volume", name, volume, owner);
}

static void
pull(variables_map const& args)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  beyond_delete("volume", name, owner);
}

static void
delete_(variables_map const& args)
{
  auto owner = self_user(ifnt, args);
  auto name = volume_name(args, owner);
  auto path = ifnt._volume_path(name);
  if (boost::filesystem::remove(path))
    report_action("deleted", "volume", name, std::string("locally"));
  else
    throw elle::Error(
      elle::sprintf("File for volume could not be deleted: %s", path));
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
  LSSharedFileListRef favorite_items =
    LSSharedFileListCreate(NULL, kLSSharedFileListFavoriteItems, NULL);
  if (!favorite_items)
    return;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
    kCFAllocatorDefault,
    reinterpret_cast<const unsigned char*>(path.data()),
    path.size(),
    true);
  LSSharedFileListItemRef item = LSSharedFileListInsertItemURL(
    favorite_items, kLSSharedFileListItemLast, NULL, NULL, url, NULL, NULL);
  if (url)
    CFRelease(url);
  if (item)
    CFRelease(item);
}

static
void
remove_path_from_finder_sidebar(std::string const& path_)
{
  LSSharedFileListRef favorite_items =
    LSSharedFileListCreate(NULL, kLSSharedFileListFavoriteItems, NULL);
  CFStringRef path = CFStringCreateWithCString(
    kCFAllocatorDefault, path_.c_str(), kCFStringEncodingUTF8);
  if (favorite_items)
  {
    CFArrayRef items_array =
      LSSharedFileListCopySnapshot(favorite_items, NULL);
    CFIndex count = CFArrayGetCount(items_array);
    LSSharedFileListItemRef item_ref;
    for (CFIndex i = 0; i < count; i++)
    {
      item_ref =
        (LSSharedFileListItemRef)CFArrayGetValueAtIndex(items_array, i);
      CFURLRef item_url = LSSharedFileListItemCopyResolvedURL(
        item_ref,
        kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes,
        NULL);
      if (item_url)
      {
        CFStringRef item_path = CFURLCopyPath(item_url);
        if (CFStringHasPrefix(item_path, path))
          LSSharedFileListItemRemove(favorite_items, item_ref);
        if (item_path)
          CFRelease(item_path);
        if (item_url)
          CFRelease(item_url);
      }
    }
    if (items_array)
      CFRelease(items_array);
  }
  if (path)
    CFRelease(path);
  if (favorite_items)
    CFRelease(favorite_items);
}
#endif

COMMAND(run)
{
  auto self = self_user(ifnt, args);
  auto name = volume_name(args, self);
  infinit::overlay::NodeEndpoints eps;
  std::vector<std::string> hosts;
  if (args.count("peer"))
  {
    hosts = args["peer"].as<std::vector<std::string>>();
    for (auto const& h: hosts)
      eps[infinit::model::Address::null].push_back(h);
  }
  auto volume = ifnt.volume_get(name);
  auto network = ifnt.network_get(volume.network, self);
  ELLE_TRACE("run network");
  bool cache = flag(args, option_cache.long_name());
  boost::optional<int> cache_size =
    option_opt<int>(args, option_cache_size.long_name());
  boost::optional<int> cache_ttl =
    option_opt<int>(args, option_cache_ttl.long_name());
  boost::optional<int> cache_invalidation =
    option_opt<int>(args, option_cache_invalidation.long_name());
  if (cache_size || cache_ttl || cache_invalidation)
    cache = true;
  reactor::scheduler().signal_handle(
    SIGINT,
    [&]
    {
      ELLE_TRACE("terminating");
      reactor::scheduler().terminate();
    });
  bool fetch = aliased_flag(args, {"fetch-endpoints", "fetch", "publish"});
  if (fetch)
    beyond_fetch_endpoints(network, eps);
  report_action("running", "network", network.name);
  auto model = network.run(
    eps, true,
    cache, cache_size, cache_ttl, cache_invalidation,
    flag(args, "async"));
  // Only push if we have are contributing storage.
  bool push =
    aliased_flag(args, {"push-endpoints", "push", "publish"}) && model->local();
  boost::optional<reactor::network::TCPServer::EndPoint> local_endpoint = {};
  if (push)
    local_endpoint = model->local()->server_endpoint();
  auto node_id = model->overlay()->node_id();
  auto run = [&]
  {
    if (push)
    {
      ELLE_DEBUG("Connect callback to log storage stat");
      model->local()->storage()->register_notifier([&] {
        network.notify_storage(self,
                               node_id);
      });

      {
        static reactor::Thread updater("periodic_stat_updater", [&] {
          while (true)
          {
            ELLE_LOG_COMPONENT("infinit-volume");
            ELLE_DEBUG(
              "Hourly notification to beyond with storage usage (periodic)");
                network.notify_storage(self,
                                       node_id);
                reactor::wait(updater, 60_min);
          }
        });
      }
    }
    ELLE_TRACE_SCOPE("run volume");
    report_action("running", "volume", volume.name);
    auto mountpoint = optional(args, "mountpoint");
    auto add_to_sidebar = flag(args, "finder-sidebar");
    auto fs = volume.run(std::move(model),
                         mountpoint
#ifdef INFINIT_MACOSX
                         , optional(args, "mount-name")
                         , optional(args, "mount-icon")
#endif
                         );
#ifdef INFINIT_MACOSX
    if (add_to_sidebar && mountpoint)
    {
      reactor::background([mountpoint]
        {
          add_path_to_finder_sidebar(mountpoint.get());
        });
    }
#endif
    elle::SafeFinally unmount([&]
    {
#ifdef INFINIT_MACOSX
      if (add_to_sidebar && mountpoint)
      {
        reactor::background([mountpoint]
          {
            remove_path_from_finder_sidebar(mountpoint.get());
          });
      }
#endif
      ELLE_TRACE("unmounting")
        fs->unmount();
    });
    if (script_mode)
    {
      reactor::FDStream stdin_stream(0);
      std::unordered_map<std::string,
        std::unique_ptr<reactor::filesystem::Handle>> handles;
      while (true)
      {
        std::string op;
        std::string pathname;
        std::string handlename;
        try
        {
          op = pathname = handlename = "";
          auto json =
            boost::any_cast<elle::json::Object>(elle::json::read(stdin_stream));
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
          if (op == "list_directory")
          {
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
            path->mkdir(0777);
          }
          else if (op == "stat")
          {
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
            response.serialize("st_blksize", st.st_blksize        );
            response.serialize("st_blocks" , st.st_blocks         );
#endif
            response.serialize("st_atime"  , uint64_t(st.st_atime));
            response.serialize("st_mtime"  , uint64_t(st.st_mtime));
            response.serialize("st_ctime"  , uint64_t(st.st_ctime));
            continue;
          }
          else if (op == "setxattr")
          {
            auto name = command.deserialize<std::string>("name");
            auto value = command.deserialize<std::string>("value");
            path->setxattr(name, value, 0);
          }
          else if (op == "getxattr")
          {
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
            auto name = command.deserialize<std::string>("name");
            path->removexattr(name);
          }
          else if (op == "link")
          {
            auto target = command.deserialize<std::string>("target");
            path->link(target);
          }
          else if (op == "symlink")
          {
            auto target = command.deserialize<std::string>("target");
            path->symlink(target);
          }
          else if (op == "readlink")
          {
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
            auto target = command.deserialize<std::string>("target");
            path->rename(target);
          }
          else if (op == "truncate")
          {
            auto sz = command.deserialize<uint64_t>("size");
            path->truncate(sz);
          }
          else if (op == "utimens")
          {
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
            auto uid = command.deserialize<int>("uid");
            auto gid = command.deserialize<int>("gid");
            path->chown(uid, gid);
          }
          else if (op == "chmod")
          {
            auto mode = command.deserialize<int>("mode");
            path->chmod(mode);
          }
          else if (op == "write_file")
          {
            auto content = command.deserialize<std::string>("content");
            auto handle = path->create(O_TRUNC|O_CREAT, 0100666);
            handle->write(elle::WeakBuffer(elle::unconst(content.data()),
                                           content.size()),
                          content.size(), 0);
            handle->close();
          }
          else if (op == "read_file")
          {
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
            path->unlink();
          }
          else if (op == "rmdir")
          {
            path->rmdir();
          }
          else if (op == "create")
          {
            int flags = command.deserialize<int>("flags");
            int mode = command.deserialize<int>("mode");
            auto handle = path->create(flags, mode);
            handles[handlename] = std::move(handle);
            handlename = "";
          }
          else if (op == "open")
          {
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
        catch (reactor::FDStream::EOF const&)
        {
          return;
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
  if (push)
  {
    elle::With<InterfacePublisher>(
      network, self, node_id, local_endpoint.get().port()) << [&]
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
    if (!volume.mountpoint)
    {
      mandatory(args, "mountpoint",
                "No default mountpoint for volume. mountpoint");
    }
  }
  run(args);
}

COMMAND(list)
{
  for (auto const& volume: ifnt.volumes_get())
  {
    std::cout << volume.name << ": network " << volume.network;
    if (volume.mountpoint)
      std::cout << " on " << volume.mountpoint.get();
    std::cout << std::endl;
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  std::vector<boost::program_options::option_description> options_run_mount = {
    { "name", value<std::string>(), "volume name" },
    { "mountpoint,m", value<std::string>(),
      "where to mount the filesystem" },
#ifdef INFINIT_MACOSX
    { "mount-name", value<std::string>(), "name of mounted volume" },
    { "mount-icon", value<std::string>(), "icon for mounted volume" },
    { "finder-sidebar", bool_switch(), "show volume in Finder sidebar" },
#endif
    { "async", bool_switch(), "use asynchronous operations" },
    option_cache,
    option_cache_size,
    option_cache_ttl,
    option_cache_invalidation,
    { "fetch-endpoints", bool_switch(),
      elle::sprintf("fetch endpoints from %s", beyond(true)).c_str() },
    { "fetch,f", bool_switch(), "alias for --fetch-endpoints" },
    { "peer", value<std::vector<std::string>>()->multitoken(),
      "peer to connect to (host:port)" },
    { "push-endpoints", bool_switch(),
      elle::sprintf("push endpoints to %s", beyond(true)).c_str() },
    { "push,p", bool_switch(), "alias for --push-endpoints" },
    { "publish", bool_switch(),
      "alias for --fetch-endpoints --push-endpoints" },
  };
  Modes modes {
    {
      "create",
      "Create a volume",
      &create,
      "--name VOLUME --network NETWORK [--mountpoint PATH]",
      {
        { "name,n", value<std::string>(), "created volume name" },
        { "network", value<std::string>(), "underlying network to use" },
        { "mountpoint,m", value<std::string>(),
          "default location to mount the volume (optional)" },
        option_output("volume"),
        { "peer", value<std::vector<std::string>>()->multitoken(),
          "peer to connect to (host:port)" },
        { "push-volume", bool_switch(),
          elle::sprintf("push the volume to %s", beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-volume" },
      },
    },
    {
      "export",
      "Export a volume for someone else to import",
      &export_,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "network to export" },
        option_output("volume"),
      },
    },
    {
      "fetch",
      elle::sprintf("Fetch a volume from %s", beyond(true)).c_str(),
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
      elle::sprintf("Push a volume to %s", beyond(true)).c_str(),
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
      options_run_mount,
    },
    {
      "mount",
      "Mount a volume",
      &mount,
      "--name VOLUME [--mountpoint PATH]",
      options_run_mount,
    },
    {
      "delete",
      "Delete a volume locally",
      &delete_,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to delete" },
      },
    },
    {
      "pull",
      elle::sprintf("Remove a volume from %s", beyond(true)).c_str(),
      &pull,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to remove" },
      },
    },
    {
      "list",
      "List volumes",
      &list,
      {},
      {},
    },
  };
  return infinit::main("Infinit volume management utility", modes, argc, argv);
}
