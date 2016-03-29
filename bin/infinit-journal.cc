#include <elle/log.hh>

#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/OKB.hh>

ELLE_LOG_COMPONENT("infinit-journal");

#include <main.hh>

infinit::Infinit ifnt;

static
bool
valid_block(boost::filesystem::path const& path)
{
  return boost::filesystem::is_regular_file(path) && !is_hidden_file(path);
}

static
std::string
human_readable_data_size(int64_t value)
{
  static const int64_t kilo = 1000;
  static const int64_t mega = pow(1000, 2);
  static const int64_t giga = pow(1000, 3);
  static const int64_t tera = pow(1000, 4);
  if (value >= tera)
    return elle::sprintf("%s TB", value / tera);
  if (value >= giga)
    return elle::sprintf("%s GB", value / giga);
  if (value >= mega)
    return elle::sprintf("%s MB", value / mega);
  if (value >= kilo)
    return elle::sprintf("%s kB", value / kilo);
  return elle::sprintf("%s B", value);
}

COMMAND(stats)
{
  auto owner = self_user(ifnt, args);
  auto network_name_ = optional(args, "network");
  std::vector<infinit::Network> networks;
  if (network_name_)
  {
    auto network_name = ifnt.qualified_name(network_name_.get(), owner);
    networks.emplace_back(ifnt.network_get(network_name, owner));
  }
  else
  {
    networks = ifnt.networks_get();
  }
  elle::json::Object res;
  for (auto const& network: networks)
  {
    boost::filesystem::path async_path = network.cache_dir() / "async";
    int operation_count = 0;
    int64_t data_size = 0;
    if (boost::filesystem::exists(async_path))
    {
      for (boost::filesystem::directory_iterator it(async_path);
           it != boost::filesystem::directory_iterator();
           ++it)
      {
        if (valid_block(it->path()))
        {
          operation_count++;
          data_size += boost::filesystem::file_size(*it);
        }
      }
    }
    if (script_mode)
    {
      elle::json::Object stats;
      stats["operations"] = operation_count;
      stats["size"] = data_size;
      res[network.name] = stats;
    }
    else
    {
      std::cout << network.name << ": "
                << operation_count << " operations, "
                << human_readable_data_size(data_size) << "\n";
    }
  }
  if (script_mode)
    elle::json::write(std::cout, res);
}

COMMAND(export_)
{
  auto owner = self_user(ifnt, args);
  auto network_name =
    ifnt.qualified_name(mandatory(args, "network", "Network"), owner);
  auto network = ifnt.network_get(network_name, owner);
  boost::filesystem::path async_path = network.cache_dir() / "async";
  auto operation = mandatory<int>(args, "operation");
  boost::filesystem::ifstream i(async_path / elle::sprintf("%s", operation));
  if (!i.good())
    throw elle::Error(elle::sprintf("no such operation: %s", operation));
  {
    elle::serialization::Context ctx;
    auto dht = network.run();
    ctx.set<infinit::model::doughnut::Doughnut*>(dht.get());
    ctx.set(infinit::model::doughnut::ACBDontWaitForSignature{});
    ctx.set(infinit::model::doughnut::OKBDontWaitForSignature{});
    auto op = elle::serialization::binary::deserialize<
      infinit::model::doughnut::consensus::Async::Op>(i, true, ctx);
    elle::serialization::json::serialize(op, std::cout);
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  Modes modes = {
    {
      "stat",
      "Show the remaining asynchronous operations count and size",
      &stats,
      "[--network NETWORK]",
      {
        { "network,N", value<std::string>(), "network to check" },
      },
    },
    {
      "export",
      "Export an operation",
      &export_,
      "--network NETWORK --operation OPERATION",
      {
        { "network,N", value<std::string>(), "network to check" },
        { "operation,O", value<int>(), "operation to export" },
      },
    },
  };
  return infinit::main("Infinit journal utility", modes, argc, argv);
}
