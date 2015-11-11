#include <elle/log.hh>
#include <elle/serialization/json.hh>

#include <reactor/FDStream.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/Local.hh>
#include <infinit/overlay/kelips/Kelips.hh>
#include <infinit/storage/Storage.hh>

ELLE_LOG_COMPONENT("infinit-volume");

#include <main.hh>

using namespace boost::program_options;
options_description mode_options("Modes");

infinit::Infinit ifnt;

static
std::string
volume_name(variables_map const& args, infinit::User const& owner)
{
  return ifnt.qualified_name(mandatory(args, "name", "volume name"), owner);
}

static
void
create(variables_map const& args)
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
    report_created("volume", name);
    ifnt.volume_save(volume);
  }
  if (aliased_flag(args, {"push-volume", "push"}))
    beyond_push("volume", name, volume, owner);
}

static
void
export_(variables_map const& args)
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

static
void
import(variables_map const& args)
{
  auto input = get_input(args);
  elle::serialization::json::SerializerIn s(*input, false);
  infinit::Volume volume(s);
  volume.mountpoint = optional(args, "mountpoint");
  ifnt.volume_save(volume);
  report_imported("volume", volume.name);
}

static
void
push(variables_map const& args)
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

static
void
fetch(variables_map const& args)
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

static
void
run(variables_map const& args)
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
  bool cache = args.count("cache");
  boost::optional<int> cache_size(0); // Not initializing warns on GCC 4.9
  if (args.count("cache") && args["cache"].as<int>() != 0)
    cache_size = args["cache"].as<int>();
  else
    cache_size.reset();
  bool async_writes =
    args.count("async-writes") && args["async-writes"].as<bool>();
  reactor::scheduler().signal_handle(
    SIGINT,
    [&]
    {
      ELLE_TRACE("terminating");
      reactor::scheduler().terminate();
    });
  bool push = aliased_flag(args, {"push-endpoints", "push", "publish"});
  bool fetch = aliased_flag(args, {"fetch-endpoints", "fetch", "publish"});
  if (fetch)
    beyond_fetch_endpoints(network, eps);
  report_action("running", "network", network.name);
  auto model = network.run(eps, true, cache, cache_size, async_writes,
      args.count("async") && args["async"].as<bool>(),
      args.count("cache-model") && args["cache-model"].as<bool>());
  auto run = [&]
  {
    ELLE_TRACE_SCOPE("run volume");
    report_action("running", "volume", volume.name);
    auto fs = volume.run(std::move(model), optional(args, "mountpoint"));
    elle::SafeFinally unmount([&]
    {
      ELLE_TRACE("unmounting")
        fs->unmount();
    });
    if (script_mode)
    {
      reactor::FDStream stdin(0);
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
            boost::any_cast<elle::json::Object>(elle::json::read(stdin));
          ELLE_TRACE("got command: %s", json);
          elle::serialization::json::SerializerIn command(json, false);
          op = command.deserialize<std::string>("operation");
          std::shared_ptr<reactor::filesystem::Path> path;
          try
          {
            pathname = command.deserialize<std::string>("path");
            path = fs->path(pathname);
          }
          catch(...) {}
          try
          {
            handlename = command.deserialize<std::string>("handle");
          } catch(...) {}
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
            response.serialize("st_size"   , st.st_size           );
            response.serialize("st_blksize", st.st_blksize        );
            response.serialize("st_blocks" , st.st_blocks         );
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
            response.serialize("f_bsize"  , uint64_t(sv.f_bsize));
            response.serialize("f_frsize" , uint64_t(sv.f_frsize));
            response.serialize("f_blocks" , sv.f_blocks);
            response.serialize("f_bfree"  , sv.f_bfree);
            response.serialize("f_bavail" , sv.f_bavail);
            response.serialize("f_files"  , sv.f_files);
            response.serialize("f_ffree"  , sv.f_ffree);
            response.serialize("f_favail" , sv.f_favail);
            response.serialize("f_fsid"   , uint64_t(sv.f_fsid));
            response.serialize("f_flag"   , uint64_t(sv.f_flag));
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
  if (push && model->local())
  {
    elle::With<InterfacePublisher>(
      network, self, model->overlay()->node_id(),
      model->local()->server_endpoint().port()) << [&]
    {
      run();
    };
  }
  else
    run();
}

static
void
mount(variables_map const& args)
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

static
void
list(variables_map const& args)
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
        option_owner,
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
        option_owner,
      },
    },
    {
      "fetch",
      elle::sprintf("fetch volume from %s", beyond(true)).c_str(),
      &fetch,
      "",
      {
        { "name,n", value<std::string>(), "volume to fetch (optional)" },
        { "network", value<std::string>(),
          "network to fetch volumes for (optional)" },
        option_owner,
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
        option_owner,
      },
    },
    {
      "run",
      "Run a volume",
      &run,
      "--name VOLUME [--mountpoint PATH]",
      {
        { "name", value<std::string>(), "volume name" },
        { "mountpoint,m", value<std::string>(),
          "where to mount the filesystem" },
        { "async", bool_switch(), "use asynchronous operations" },
        { "async-writes", bool_switch(),
          "do not wait for writes on the backend" },
        { "cache", value<int>()->implicit_value(0),
          "enable storage caching, "
          "optional argument specifies maximum size in bytes" },
        { "cache-model", bool_switch(), "enable model caching" },
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
        option_owner,
      },
    },
    {
      "mount",
      "Mount a volume",
      &mount,
      "--name VOLUME [--mountpoint PATH]",
      {
        { "name", value<std::string>(), "volume name" },
        { "mountpoint,m", value<std::string>(),
          "where to mount the filesystem" },
        { "async", bool_switch(), "use asynchronous operations" },
        { "async-writes", bool_switch(),
          "do not wait for writes on the backend" },
        { "cache", value<int>(),
          "enable storage caching, "
          "optional argument specifies maximum size in bytes (default: 0)" },
        { "cache-model", bool_switch(), "enable model caching" },
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
        option_owner,
      },
    },
    {
      "delete",
      "Delete a volume",
      &delete_,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to delete" },
        option_owner,
      },
    },
    {
      "pull",
      elle::sprintf("Remove a volume from %s", beyond(true)).c_str(),
      &pull,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to remove" },
        option_owner,
      },
    },
    {
      "list",
      "List volumes",
      &list,
      {},
      {
        option_owner,
      }
    },
  };
  return infinit::main("Infinit volume management utility", modes, argc, argv);
}
