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
  std::unordered_map<infinit::model::Address, std::vector<std::string>> hosts;
  bool fetch = aliased_flag(args, {"fetch-endpoints", "fetch"});
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
    args["volume"].as<std::string>(), std::move(model));
  new infinit::smb::SMBServer(std::move(fs));
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
        { "name,n", value<std::string>(), "network name" },
        { "volume", value<std::string>(), "volume name" },
        { "peer", value<std::vector<std::string>>()->multitoken(),
          "peer to connect to (host:port)" },
        { "async", bool_switch(), "use asynchronous operations" },
        { "cache-model", bool_switch(), "enable model caching" },
        { "fetch-endpoints", bool_switch(),
          elle::sprintf("fetch endpoints from %s", beyond()).c_str() },
        { "fetch,f", bool_switch(), "alias for --fetch-endpoints" },
        option_owner,
      },
    },
  };
  return infinit::main("Infinit SMB adapter", modes, argc, argv);
}
