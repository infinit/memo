#include <elle/log.hh>

#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/Async.hh>
#include <infinit/model/doughnut/OKB.hh>

ELLE_LOG_COMPONENT("infinit-journal");

#include <main.hh>

infinit::Infinit ifnt;

namespace fs = boost::filesystem;

namespace
{
  bool
  valid_block(fs::path const& path)
  {
    return fs::is_regular_file(path) && !infinit::is_hidden_file(path);
  }

  std::string
  human_readable_data_size(double value)
  {
    constexpr double kilo = 1000;
    constexpr double mega = 1000 * kilo;
    constexpr double giga = 1000 * mega;
    constexpr double tera = 1000 * giga;
    if (value >= tera)
      return elle::sprintf("%.2f TB", value / tera);
    else if (value >= giga)
      return elle::sprintf("%.2f GB", value / giga);
    else if (value >= mega)
      return elle::sprintf("%.1f MB", value / mega);
    else if (value >= kilo)
      return elle::sprintf("%.f kB", value / kilo);
    else
      return elle::sprintf("%.f B", value);
  }
}

COMMAND(stats)
{
  auto owner = self_user(ifnt, args);
  auto network_name_ = optional(args, "network");
  auto networks = std::vector<infinit::Network>{};
  if (network_name_)
  {
    auto network_name = ifnt.qualified_name(network_name_.get(), owner);
    networks.emplace_back(ifnt.network_get(network_name, owner));
  }
  else
  {
    networks = ifnt.networks_get(owner);
  }
  auto res = elle::json::Object{};
  for (auto const& network: networks)
  {
    fs::path async_path = network.cache_dir(owner) / "async";
    int operation_count = 0;
    int64_t data_size = 0;
    if (fs::exists(async_path))
      for (auto it = fs::directory_iterator(async_path);
           it != fs::directory_iterator();
           ++it)
        if (valid_block(it->path()))
        {
          operation_count++;
          data_size += fs::file_size(*it);
        }
    if (script_mode)
      res[network.name] = elle::json::Object
        {
          {"operations", operation_count},
          {"size", data_size},
        };
    else
      elle::printf("%s: %s operations, %s\n",
                   network.name, operation_count,
                   human_readable_data_size(data_size));
  }
  if (script_mode)
    elle::json::write(std::cout, res);
}

namespace
{
   elle::serialization::Context
   context(infinit::User const& owner,
           std::unique_ptr<infinit::model::doughnut::Doughnut> const& dht)
   {
     auto ctx = elle::serialization::Context{};
     ctx.set<infinit::model::doughnut::Doughnut*>(dht.get());
     ctx.set(infinit::model::doughnut::ACBDontWaitForSignature{});
     ctx.set(infinit::model::doughnut::OKBDontWaitForSignature{});
     return ctx;
   }
}

COMMAND(export_)
{
  auto owner = self_user(ifnt, args);
  auto network = ifnt.network_get(
    ifnt.qualified_name(mandatory(args, "network", "Network"), owner),
    owner);
  auto id = elle::sprintf("%s", mandatory<int>(args, "operation"));
  auto path = network.cache_dir(owner) / "async" / id;
  fs::ifstream f;
  ifnt._open_read(f, path, id, "operation");
  auto dht = network.run(owner);
  auto ctx = context(owner, dht);
  auto op = elle::serialization::binary::deserialize<
    infinit::model::doughnut::consensus::Async::Op>(f, true, ctx);
  elle::serialization::json::serialize(op, std::cout);
}

COMMAND(describe)
{
  auto owner = self_user(ifnt, args);
  auto network = ifnt.network_get(
    ifnt.qualified_name(mandatory(args, "network", "Network"), owner),
    owner);
  auto dht = network.run(owner);
  auto ctx = context(owner, dht);
  fs::path async_path = network.cache_dir(owner) / "async";
  auto operation = optional<int>(args, "operation");
  auto report = [&] (fs::path const& path)
    {
      fs::ifstream f;
      ifnt._open_read(f, path, path.filename().string(), "operation");
      auto name = path.filename().string();
      std::cout << name << ": ";
      try
      {
        auto op = elle::serialization::binary::deserialize<
          infinit::model::doughnut::consensus::Async::Op>(
            f, true, ctx);
        if (op.resolver)
          std::cout << op.resolver->description();
        else
          std::cout << "no description for this operation";
      }
      catch (elle::serialization::Error const&)
      {
        std::cerr << "error: " << elle::exception_string();
      }
      std::cout << std::endl;
    };
  if (operation)
    report(async_path / elle::sprintf("%s", *operation));
  else
    for (auto const& path:
         infinit::model::doughnut::consensus::Async::entries(async_path))
      report(path);
}

int
main(int argc, char** argv)
{
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
   {
      "describe",
      "Describe asynchronous operation(s)",
      &describe,
      "--network NETWORK [--operation OPERATION]",
      {
        { "network,N", value<std::string>(), "network to check" },
        { "operation,O", value<int>(), "operation to describe" },
      },
    },
  };
  return infinit::main("Infinit journal utility", modes, argc, argv);
}
