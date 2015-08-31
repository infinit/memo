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
  return ifnt.qualified_name(mandatory(args, "name", "volume name"),
                             owner.public_key);
}

static
void
create(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto name = volume_name(args, owner);
  auto network_name = ifnt.qualified_name(mandatory(args, "network"),
                                          owner.public_key);
  auto mountpoint = optional(args, "mountpoint");
  auto network = ifnt.network_get(network_name);
  ELLE_TRACE("start network");
  report_action("starting", "network", network.qualified_name());
  auto model = network.run();
  ELLE_TRACE("create volume");
  report("creating volume root blocks");
  auto fs = elle::make_unique<infinit::filesystem::FileSystem>(model.second);
  infinit::Volume volume(
    name, mountpoint, fs->root_address(), network.qualified_name());
  if (args.count("stdout") && args["stdout"].as<bool>())
  {
    elle::serialization::json::SerializerOut s(std::cout, false);
    s.serialize_forward(volume);
  }
  else
  {
    report_created("volume", name);
    ifnt.volume_save(volume);
  }
}

static
void
export_(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
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
publish(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto name = volume_name(args, owner);
  auto volume = ifnt.volume_get(name);
  auto network = ifnt.network_get(volume.network);
  auto owner_uid = infinit::User::uid(network.dht()->owner);
  beyond_publish("volume", name, volume);
}

static
void
fetch(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto name = volume_name(args, owner);
  auto desc =
    beyond_fetch<infinit::Volume>("volume", name);
  ifnt.volume_save(std::move(desc));
}

static
void
run(variables_map const& args)
{
  auto owner = ifnt.user_get(optional(args, option_owner.long_name()));
  auto name = volume_name(args, owner);
  std::vector<std::string> hosts;
  if (args.count("host"))
    hosts = args["host"].as<std::vector<std::string>>();
  auto volume = ifnt.volume_get(name);
  auto network = ifnt.network_get(volume.network);
  ELLE_TRACE("run network");
  bool cache = args.count("cache");
  boost::optional<int> cache_size;
  if (args.count("cache") && args["cache"].as<int>() != 0)
    cache_size = args["cache"].as<int>();
  bool async_writes =
    args.count("async-writes") && args["async-writes"].as<bool>();
  report_action("running", "network", volume.name);
  auto model = network.run(hosts, true, cache, cache_size, async_writes);
  ELLE_TRACE("run volume");
  report_action("running", "volume", volume.name);
  auto fs = volume.run(model.second, optional(args, "mountpoint"));
  reactor::scheduler().signal_handle(
    SIGINT,
    [&]
    {
      ELLE_TRACE("terminating");
      reactor::scheduler().terminate();
    });
  elle::SafeFinally unmount(
    [&]
    {
      ELLE_TRACE("unmounting")
        fs->unmount();
    });
  if (script_mode)
  {
    reactor::FDStream stdin(0);
    while (true)
    {
      try
      {
        auto json =
          boost::any_cast<elle::json::Object>(elle::json::read(stdin));
        ELLE_TRACE("got command: %s", json);
        elle::serialization::json::SerializerIn command(json, false);
        auto op = command.deserialize<std::string>("operation");
        auto path =
          fs->path(command.deserialize<std::string>("path"));
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
          continue;
        }
        else if (op == "mkdir")
        {
          path->mkdir(0777);
        }
        else
          throw elle::Error(elle::sprintf("operation %s does not exist", op));
        elle::serialization::json::SerializerOut response(std::cout);
        response.serialize("success", true);
      }
      catch (reactor::FDStream::EOF const&)
      {
        return;
      }
      catch (elle::Error const& e)
      {
        elle::serialization::json::SerializerOut response(std::cout);
        response.serialize("success", false);
        response.serialize("message", e.what());
      }
    }
  }
  else
  {
    ELLE_TRACE("wait filesystem");
    reactor::wait(*fs);
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
        { "name", value<std::string>(), "created volume name" },
        { "network", value<std::string>(), "underlying network to use" },
        { "mountpoint", value<std::string>(), "where to mount the filesystem" },
        option_owner,
        { "stdout", bool_switch(), "output configuration to stdout" },
      },
    },
    {
      "export",
      "export a network for someone else to import",
      &export_,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "network to export" },
        { "output,o", value<std::string>(),
          "file to write volume to  (stdout by default)"},
        option_owner,
      },
    },
    {
      "fetch",
      elle::sprintf("fetch network from %s", beyond()).c_str(),
      &fetch,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "volume to fetch" },
        option_owner,
      },
    },
    {
      "import",
      "Import a volume",
      &import,
      "",
      {
        { "input,i", value<std::string>(),
          "file to read volume from (defaults to stdin)" },
        { "mountpoint", value<std::string>(), "where to mount the filesystem" },
      },
    },
    {
      "publish",
      elle::sprintf("publish volume to %s", beyond()).c_str(),
      &publish,
      "--name VOLUME",
      {
        { "name,n", value<std::string>(), "volume to publish" },
        option_owner,
      },
    },
    {
      "run",
      "Run a volume",
      &run,
      "--name VOLUME [--mountpoint PATH]",
      {
        { "async-writes,a", bool_switch(),
          "do not wait for writes on the backend" },
        { "cache,c", value<int>()->implicit_value(0),
          "enable storage caching, "
          "optional arguments specifies maximum size in bytes" },
        { "mountpoint,m", value<std::string>(),
          "where to mount the filesystem" },
        { "name", value<std::string>(), "volume name" },
        { "host", value<std::vector<std::string>>()->multitoken(),
          "hosts to connect to" },
        option_owner,
      },
    },
  };
  return infinit::main("Infinit volume management utility", modes, argc, argv);
}
