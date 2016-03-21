#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit-journal");

#include <main.hh>

infinit::Infinit ifnt;

Mode::OptionDescription option_data(
  "data", boost::program_options::bool_switch(),
  "data in asynchronous cache");
Mode::OptionDescription option_operations(
  "operations", boost::program_options::bool_switch(),
  "number of asynchronous operations remaining");

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

COMMAND(show)
{
  auto owner = self_user(ifnt, args);
  auto network_name =
    ifnt.qualified_name(mandatory(args, "network", "Network"), owner);
  auto network = ifnt.network_get(network_name, owner);
  bool data = flag(args, option_data);
  bool operations = flag(args, option_operations);
  if (!data && !operations)
  {
    throw CommandLineError("specify either --%s or --%s",
      option_data.long_name(), option_operations.long_name());
  }
  boost::filesystem::path async_path = network.cache_dir() / "async";
  int operation_count = 0;
  int64_t data_size = 0;
  if (boost::filesystem::exists(async_path))
  {
    if (data)
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
    if (operations && !data)
    {
      operation_count = std::count_if(
        boost::filesystem::directory_iterator(async_path),
        boost::filesystem::directory_iterator(),
        [](boost::filesystem::directory_entry const& entry)
        {
          return valid_block(entry.path());
        });
    }
  }
  std::cout << network.name << ": ";
  if (operations)
  {
    std::cout << operation_count <<  " operations";
    if (data)
      std::cout << ", ";
  }
  if (data)
    std::cout << human_readable_data_size(data_size);
  std::cout << std::endl;
}

int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  Modes modes = {
    {
      "show",
      "Show async operations",
      &show,
      elle::sprintf("--network NETWORK [--%s --%s]",
                    option_data.long_name(), option_operations.long_name()),
      {
        { "network,N",  value<std::string>(), "network to check" },
        option_data,
        option_operations,
      },
    },
  };
  return infinit::main("Infinit journal utility", modes, argc, argv);
}
