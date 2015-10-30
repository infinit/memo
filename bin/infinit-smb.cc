#include <infinit/smb/smb.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/filesystem/filesystem.hh>

#include <elle/serialization/json.hh>

#include <reactor/scheduler.hh>

ELLE_LOG_COMPONENT("infinit-smb");

#include <main.hh>

using namespace boost::program_options;

infinit::Infinit ifnt;

void run(variables_map const& args)
{
  auto name = mandatory(args, "name", "network name");
  auto self = self_user(ifnt, args);
  auto network = ifnt.network_get(name, self);
  std::unordered_map<elle::UUID, std::vector<std::string>> hosts;
  bool push = args.count("push") && args["push"].as<bool>();
  bool fetch = args.count("fetch") && args["fetch"].as<bool>();
  if (fetch)
    beyond_fetch_endpoints(network, hosts);
  bool cache = args.count("cache");
  boost::optional<int> cache_size(0); // Not initializing warns on GCC 4.9
  if (args.count("cache") && args["cache"].as<int>() != 0)
    cache_size = args["cache"].as<int>();
  else
    cache_size.reset();
  bool async_writes =
    args.count("async-writes") && args["async-writes"].as<bool>();
  report_action("running", "network", network.name);
  auto model = network.run(hosts, true, cache, cache_size, async_writes,
      args.count("async") && args["async"].as<bool>(),
      args.count("cache-model") && args["cache-model"].as<bool>());
  auto fs = elle::make_unique<infinit::filesystem::FileSystem>(
    args["volume"].as<std::string>(), model.second);
  auto smb = new infinit::smb::SMBServer(std::move(fs));
  reactor::sleep();
}
  
  
int main(int argc, char** argv)
{
  Modes modes {
    {
      "run",
      "Run",
      &run,
      "--name NETWORK",
      {
        option_owner,
        { "fetch", bool_switch(),
            elle::sprintf("fetch endpoints from %s", beyond()).c_str() },
        { "peer", value<std::vector<std::string>>()->multitoken(),
            "peer to connect to (host:port)" },
        { "name", value<std::string>(), "created network name" },
        { "volume", value<std::string>(), "created volume name" },
        { "push", bool_switch(),
            elle::sprintf("push endpoints to %s", beyond()).c_str() },
        { "async", bool_switch(), "Use asynchronious operations" },
        { "cache-model", bool_switch(), "Enable model caching"},
      },
    },
  };
  return infinit::main("Infinit SMB adapter", modes, argc, argv);
}