#include <elle/log.hh>
#include <elle/serialization/json.hh>

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
volume_name(boost::program_options::variables_map args)
{
  return ifnt.qualified_name(mandatory(args, "name", "volume name"));
}

static
void
create(variables_map const& args)
{
  auto name = volume_name(args);
  auto network_name = mandatory(args, "network");
  auto mountpoint = optional(args, "mountpoint");
  auto network = ifnt.network_get(network_name);
  ELLE_TRACE("start network");
  auto model = network.run();
  ELLE_TRACE("create volume");
  auto fs = elle::make_unique<infinit::filesystem::FileSystem>(model.second);
  infinit::Volume volume(name, mountpoint, fs->root_address(), network.name);
  {
    if (args.count("stdout") && args["stdout"].as<bool>())
      ifnt.volume_save(volume);
    else
    {
      elle::serialization::json::SerializerOut s(std::cout, false);
      s.serialize_forward(volume);
    }
  }
}

static
void
export_(variables_map const& args)
{
  auto name = volume_name(args);
  auto output = get_output(args);
  auto volume = ifnt.volume_get(name);
  volume.mountpoint.reset();
  {
    elle::serialization::json::SerializerOut s(*output, false);
    s.serialize_forward(volume);
  }
}

static
void
import(variables_map const& args)
{
  auto input = get_input(args);
  {
    elle::serialization::json::SerializerIn s(*input, false);
    infinit::Volume volume(s);
    volume.mountpoint = optional(args, "mountpoint");
    ifnt.volume_save(volume);
  }
}

static
void
publish(variables_map const& args)
{
  auto name = volume_name(args);
  auto volume = ifnt.volume_get(name);
  auto network = ifnt.network_get(volume.network);
  auto owner_uid = infinit::User::uid(network.dht()->owner);
  beyond_publish("volume", name, volume);
}

static
void
fetch(variables_map const& args)
{
  auto name = volume_name(args);
  auto desc =
    beyond_fetch<infinit::Volume>("volume", name);
  ifnt.volume_save(std::move(desc));
}

static
void
run(variables_map const& args)
{
  auto name = volume_name(args);
  std::vector<std::string> hosts;
  if (args.count("host"))
    hosts = args["host"].as<std::vector<std::string>>();
  auto volume = ifnt.volume_get(name);
  auto network = ifnt.network_get(volume.network);
  ELLE_TRACE("run network");
  bool cache = args.count("cache") && args["cache"].as<bool>();
  bool async_writes =
    args.count("async-writes") && args["async-writes"].as<bool>();
  boost::optional<int> cache_size;
  if (args.count("cache-size"))
  {
    cache = true;
    cache_size = args["cache-size"].as<int>();
  }
  auto model = network.run(hosts, true, cache, cache_size, async_writes);
  ELLE_TRACE("run volume");
  auto fs = volume.run(model.second, optional(args, "mountpoint"));
  ELLE_TRACE("wait");
  reactor::wait(*fs);
}

int main(int argc, char** argv)
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
          "file to write user to  (stdout by default)"}
      },
    },
    {
      "fetch",
      elle::sprintf("fetch network from %s", beyond()).c_str(),
      &fetch,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "volume to fetch" },
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
        { "host", value<std::vector<std::string>>()->multitoken(),
          "hosts to connect to" },
        { "cache,c", bool_switch(), "enable storage caching" },
        { "cache-size,s", value<int>(),
          "maximum storage cache in bytes (implies --cache)" },
        { "async-writes,a", bool_switch(),
          "do not wait for writes on the backend" },
      },
    },
  };
  return infinit::main("Infinit volume management utility", modes, argc, argv);
}
