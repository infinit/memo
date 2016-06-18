#include <unordered_map>
#include <utility>
#include <map>

#include <elle/log.hh>
#include <elle/log/TextLogger.hh>
#include <elle/string/algorithm.hh>
#include <elle/system/Process.hh>
#include <elle/os/environ.hh>

#include <infinit/storage/Dropbox.hh>
#include <infinit/storage/Filesystem.hh>
#include <infinit/storage/GCS.hh>
#include <infinit/storage/GoogleDrive.hh>
#include <infinit/storage/Strip.hh>
#ifndef INFINIT_WINDOWS
# include <infinit/storage/sftp.hh>
#endif
#include <infinit/storage/S3.hh>

#include <reactor/connectivity/connectivity.hh>
#include <reactor/network/upnp.hh>
#include <reactor/scheduler.hh>

ELLE_LOG_COMPONENT("infinit-doctor");

#include <main.hh>

infinit::Infinit ifnt;

static
std::string
result(bool value)
{
  return value ? "Good" : "Bad";
}

// Return the infinit related environment.
static
std::unordered_map<std::string, std::string>
infinit_related_environment()
{
  auto environ = elle::os::environ();
  // Remove non INFINIT_ or ELLE_ prefixed entries.
  for (auto it = environ.begin(); it != environ.end();)
  {
    if (it->first.find("INFINIT_") != 0 &&
        it->first.find("ELLE_") != 0)
      environ.erase(it++);
    else
      ++it;
  }
  return environ;
}

static
bool
_networking(boost::program_options::variables_map const& args,
            boost::optional<std::ostream&> output_stream)
{
  bool sane = true;
  bool verbose = flag(args, "verbose");
  // Contact beyond.
  if (verbose)
    std::cout << "Contacting " << beyond() << std::endl;
  {
    try
    {
      reactor::http::Request r(beyond(), reactor::http::Method::GET);
      reactor::wait(r);
      auto status = (r.status() == reactor::http::StatusCode::OK);
      sane &= status;
      if (verbose || !status)
      {
        auto& output = status ? std::cout : std::cerr;
        elle::fprintf(output, "  Able to contact %s%s: ",
                      beyond(),
                      !status
                      ? elle::sprintf(" but got a %s error", r.status())
                      : std::string{});
        if (verbose)
          output << "ok" << std::endl;
      }
    }
    catch (elle::Error const&)
    {
      if (!verbose)
        std::cerr << "Contacting " << beyond() << std::endl;
      elle::fprintf(std::cerr, "  Unable to contact %s: %s\n", beyond(),
                    elle::exception_string());
    }
  }
  if (verbose)
    std::cout << std::endl;
  // Interfaces.
  auto interfaces = elle::network::Interface::get_map(
    elle::network::Interface::Filter::no_loopback);
  if (verbose)
    std::cout << "Local IP Addresses:" << std::endl;
  std::vector<std::string> public_ips;
  for (auto i: interfaces)
  {
    if (i.second.ipv4_address.empty())
      continue;
    if (verbose)
      std::cout << "  " << i.second.ipv4_address << std::endl;
    public_ips.push_back(i.second.ipv4_address);
  }

  if (verbose)
    std::cout << "\nConnectivity:" << std::endl;
  // XXX: This should be nat.infinit.sh or something.
  std::string host = "192.241.139.66";
  uint16_t port = 5456;
  auto run = [&] (std::string const& name,
                  std::function<reactor::connectivity::Result (
                    std::string const& host,
                    uint16_t port)> const& function,
                  int deltaport = 0)
  {
    std::string result = elle::sprintf("  %s: ", name);
    auto status = true;
    try
    {
      auto address = function(host, port + deltaport);
      result += address.host;
      if (std::find(public_ips.begin(), public_ips.end(), address.host) ==
          public_ips.end())
        result += " (external)";
      else
        result += " (internal)";
      if (address.local_port)
      {
        result += " ";
        if (address.local_port == address.remote_port)
          result += "same port";
        else
          result += elle::sprintf("different ports (local: %s, remote: %s)",
                               address.local_port, address.remote_port);
      }
    }
    catch (...)
    {
      result += elle::exception_string();
      status = false;
    }
    sane &= status;
    if (!status || verbose)
    {
      auto& output = status ? std::cout : std::cerr;
      output << result << std::endl;
    }
  };
  run("TCP", reactor::connectivity::tcp);
  run("UDP", reactor::connectivity::udp);
  run("UTP",
      std::bind(reactor::connectivity::utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0),
      1);
  run("UTP (XOR)",
      std::bind(reactor::connectivity::utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0xFF),
      2);
  run("RDV UTP",
      std::bind(reactor::connectivity::rdv_utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0),
      1);
  run("RDV UTP (XOR)",
      std::bind(reactor::connectivity::rdv_utp,
                std::placeholders::_1,
                std::placeholders::_2,
                0xFF),
      2);
  {
    std::stringstream nat;
    nat << "  NAT ";
    bool status = true;
    try
    {
      nat << reactor::connectivity::nat(host, port) << std::endl;
    }
    catch (std::runtime_error const&)
    {
      nat << elle::exception_string() << std::endl;
      status = false;
    }
    sane &= status;
    if (!status || verbose)
    {
      auto& output = status ? std::cout : std::cerr;
      output << nat.str() << std::endl;
    }
  }
  {
    bool status = true;
    std::stringstream out;
    out << "  UPNP:" << std::endl;
    auto upnp = reactor::network::UPNP::make();
    try
    {
      upnp->initialize();
      out << "    available: " << upnp->available() << std::endl;
      auto ip = upnp->external_ip();
      out << "    external_ip: " << ip << std::endl;
      auto pm = upnp->setup_redirect(reactor::network::Protocol::tcp, 5678);
      out << "    mapping: " << pm.internal_host << ':' << pm.internal_port
          << " -> " << pm.external_host << ':' << pm.external_port << std::endl;
      auto pm2 = upnp->setup_redirect(reactor::network::Protocol::udt, 5679);
      out << "    mapping: " << pm2.internal_host << ':' << pm2.internal_port
          << " -> " << pm2.external_host << ':' << pm2.external_port << std::endl;
    }
    catch (std::exception const& e)
    {
      out << "    exception: " << e.what() << std::endl;
      status = false;
    }
    sane &= status;
    if (!status || verbose)
    {
      auto& output = status ? std::cout : std::cerr;
      output << out.str() << std::endl;
    }
  }
  return sane;
}

// Return the current user permissions on a given path.
static
std::pair<bool, bool>
permissions(boost::filesystem::path const& path)
{
  if (!boost::filesystem::exists(path))
    throw elle::Error("Doesn't exist");
  auto s = boost::filesystem::status(path);
  bool read = s.permissions() & boost::filesystem::perms::owner_read;
  bool write = s.permissions() & boost::filesystem::perms::owner_write;
  return std::make_pair(read, write);
}

// Return the permissions as a string:
// r, w, rw, None, ...
static
std::string
permissions_string(boost::filesystem::path const& path)
{
  try
  {
    auto perms = permissions(path);
    std::string res = "";
    if (perms.first)
      res += "r";
    if (perms.second)
      res += "w";
    if (res.length() == 0)
      res = "None";
    return res;
  }
  catch (...)
  {
    return elle::exception_string();
  }
}

//
static
std::pair<bool, std::string>
has_permission(boost::filesystem::path const& path,
               bool mandatory = true)
{
  auto res = permissions_string(path);
  auto good = (res == "rw");
  auto sane = (good || !mandatory);
  return std::make_pair(sane, res);
}

static
bool
permission(boost::filesystem::path const& path,
           bool verbose,
           bool mandatory = true,
           uint32_t indent = 0)
{
  auto sane = has_permission(path, mandatory);
  if (verbose || !sane.first)
  {
    auto& output = sane.first ? std::cout : std::cerr;
    output << std::string(indent, ' ') << path.string() << ": "
           << (sane.first ? std::string{"ok"} : sane.second) << std::endl;
  }
  return sane.first;
}

static
bool
permissions(bool verbose)
{
  if (verbose)
    std::cout << "Permissions:" << std::endl;
  bool sane = true;
  sane &= permission(elle::system::home_directory(), verbose, true, 2);
  sane &= permission(infinit::xdg_cache_home(), verbose, false, 2);
  sane &= permission(infinit::xdg_config_home(), verbose, false, 2);
  sane &= permission(infinit::xdg_data_home(), verbose, false, 2);
  sane &= permission(infinit::xdg_state_home(), verbose, false, 2);
  return sane;
}

static
bool
environment(bool verbose)
{
  auto env = infinit_related_environment();
  if (verbose)
  {
    std::cout << "Environment:" << std::endl;
    for (auto const& entry: env)
      std::cout << "  " << entry.first << "=" << entry.second << std::endl;
  }
  return true;
}

static
bool
space_left(bool verbose, double min_ratio, uint32_t min_absolute)
{
  auto f = boost::filesystem::space(infinit::xdg_data_home());
  double ratio = f.available / (double) f.capacity;
  bool full = (ratio < min_ratio) || (f.available < min_absolute);
  if (verbose || full)
  {
    auto& output = full ? std::cerr : std::cout;
    output << "Space:" << std::endl;
    output << "  space left: " << f.available << " (" << ratio * 100 << "%)" << std::endl;
  }
  return true;
}

static
bool
fuse(bool verbose)
{
#if 0
  try
  {
    elle::system::Process p({"fusermount", "-V"});
    if (verbose)
      std::cout << "fuse: ok" << std::endl;
    return true;
  }
  catch (...)
  {
    return false;
  }
#else
  return true;
#endif
}

static
bool
_sanity(boost::program_options::variables_map const& args,
        boost::optional<std::ostream&> output_stream)
{
  bool sane = true;
  auto test = [] (std::function<void()> const& todo,
                  std::string message = "") -> bool
  {
    try
    {
      todo();
      return true;
    }
    catch (...)
    {
      if (!message.empty())
        std::cerr << elle::exception_string() << std::endl;
      else
        std::cerr << message << std::endl;
      return false;
    }
  };
  bool verbose = flag(args, "verbose");
  if (verbose)
  {
    std::cout << "Disclaimer: " << std::endl;
    std::cout << "" << std::endl;
    std::cout << std::endl;
  }

  // User name.
  sane &= test(
    [&] {
      auto self_name = self_user_name();
      if (verbose)
      {
        std::cout << "User:" << std::endl;
        std::cout << "  default user name: " << self_name << std::endl;
      }
      // static const boost::regex allowed("${name_regex}");
      // boost::smatch str_matches;
      // if (!boost::regex_match(test_name, str_matches, allowed))
      //   throw elle::Error("You won't be able to push this user on beyond");
    }, "Invalid or unrecognized user name");
  sane &= space_left(verbose, 0.02, 50 * 1024 * 1024);
  sane &= environment(verbose);
  sane &= permissions(verbose);
  sane &= fuse(verbose);
  return sane;
}

template <typename T>
std::map<std::string, std::pair<T, bool>>
parse(std::vector<T>& container)
{
  std::map<std::string, std::pair<T, bool>> output;
  for (auto& item: container)
  {
    auto name = item.name;
    output.emplace(std::piecewise_construct,
                   std::forward_as_tuple(name),
                   std::forward_as_tuple(std::move(item), false));
  }
  return output;
}

template <typename T>
std::map<std::string, std::pair<std::unique_ptr<T>, bool>>
parse(std::vector<std::unique_ptr<T>>& container)
{
  std::map<std::string, std::pair<std::unique_ptr<T>, bool>> output;
  for (auto& item: container)
  {
    auto name = item->name;
    output.emplace(std::piecewise_construct,
                   std::forward_as_tuple(name),
                   std::forward_as_tuple(std::move(item), false));
  }
  return output;
}

static
bool
_integrity(boost::program_options::variables_map const& args,
           boost::optional<std::ostream&> output_stream)
{
  bool sane = true;
#define CONVERT(entity_getter, ...)                       \
  [] () {                                                 \
    auto entities = entity_getter(__VA_ARGS__);           \
    return parse(entities);                               \
  }()

  auto users = CONVERT(ifnt.users_get);
  auto aws_credentials = ifnt.credentials_aws();
  auto gcs_credentials = ifnt.credentials_gcs();
  auto storage_resources = CONVERT(ifnt.storages_get);
  auto drives = CONVERT(ifnt.drives_get);
  auto volumes = CONVERT(ifnt.volumes_get);
  auto networks = CONVERT(ifnt.networks_get);
  auto verbose = flag(args, "verbose");
  std::cout << "Storage resources:" << std::endl;
  for (auto& elem: storage_resources)
  {
    auto& storage = elem.second.first;
    auto& status = elem.second.second;
    if (auto s3config = dynamic_cast<infinit::storage::S3StorageConfig const*>(storage.get()))
    {
      auto it =
        std::find_if(
          aws_credentials.begin(),
          aws_credentials.end(),
          [&s3config] (std::unique_ptr<infinit::AWSCredentials, std::default_delete<infinit::Credentials>> const& credentials)
          {
            #define COMPARE(field) (credentials->field == s3config->credentials.field())
                return COMPARE(access_key_id) && COMPARE(secret_access_key);
            #undef COMPARE
          });
      status = (it != aws_credentials.end());
      sane &= status;
      if (verbose || !status)
      {
        auto& output = !status ? std::cerr : std::cout;
        elle::fprintf(output, "  [%s] %s (AWS):\n", result(status), storage->name);
        if (status)
          elle::fprintf(output, "    [%s] Credentials: %s\n",
                result(status), (*it)->display_name());
        else
          elle::fprintf(output, "    [%s] Missing credentials\n", result(status));
      }
    }
    if (auto fsconfig = dynamic_cast<infinit::storage::FilesystemStorageConfig const*>(storage.get()))
    {
      status = has_permission(fsconfig->path).first;
      if (verbose || !status)
      {
        auto& output = !status ? std::cerr : std::cout;
        elle::fprintf(output, "  [%s] %s (Filesystem):\n", result(status), storage->name);
        elle::fprintf(output, "    [%s] ", result(has_permission(fsconfig->path, true).first));
        permission(boost::filesystem::path(fsconfig->path), true);
      }
    }
    if (auto gcsconfig = dynamic_cast<infinit::storage::GCSConfig const*>(storage.get()))
    {
      auto it =
        std::find_if(
          gcs_credentials.begin(),
          gcs_credentials.end(),
          [&gcsconfig] (std::unique_ptr<infinit::OAuthCredentials, std::default_delete<infinit::Credentials>> const& credentials)
          {
            return credentials->refresh_token == gcsconfig->refresh_token;
          });
      status = (it != gcs_credentials.end());
      sane &= status;
      if (verbose || !status)
      {
        auto& output = !status ? std::cerr : std::cout;
        elle::fprintf(output, "  [%s] %s (GCS):\n", result(status), storage->name);
        if (status)
          elle::fprintf(output, "    [%s] Credentials: %s\n",
                result(status), (*it)->display_name());
        else
          elle::fprintf(output, "    [%s] Missing credentials\n", result(status));
      }
    }
#ifndef INFINIT_WINDOWS
    if (/* auto ssh = */
      dynamic_cast<infinit::storage::SFTPStorageConfig const*>(storage.get()))
    {
      // XXX:
    }
#endif
  }
  std::cout << "Networks:" << std::endl;
  for (auto& elem: networks)
  {
    auto const& network = elem.second.first;
    auto& status = elem.second.second;
    std::vector<std::string> storage_names;
    if (network.model)
    {
      if (network.model->storage)
      {
        if (auto strip = dynamic_cast<infinit::storage::StripStorageConfig*>(
              network.model->storage.get()))
          for (auto const& s: strip->storage)
            storage_names.push_back(s->name);
        else
          storage_names.push_back(network.model->storage->name);
      }
    }
    status = storage_names.size() == 0 || std::all_of(
      storage_names.begin(),
      storage_names.end(),
      [&] (std::string const& name) -> bool
      {
        auto it = storage_resources.find(name);
        return (it != storage_resources.end() && it->second.second);
      });
    sane &= status;
    if (verbose || !status)
    {
      auto& output = !status ? std::cerr : std::cout;
      elle::fprintf(output, "  [%s] %s:\n", result(status), network.name);
      for (auto storage: storage_names)
      {
        auto it = storage_resources.find(storage);
        if (it == storage_resources.end())
          elle::fprintf(
            output, "    [%s] storage resource %s: Missing", result(false), storage);
        else
          elle::fprintf(
            output, "    [%s] storage resource %s", result(it->second.second), storage);
        output << std::endl;
      }
    }
  }
  std::cout << "Volumes:" << std::endl;
  for (auto& elems: volumes)
  {
    auto const& volume = elems.second.first;
    auto& status = elems.second.second;
    auto network = networks.find(volume.network);
    auto network_presents = network != networks.end();
    status = network_presents && network->second.second;
    sane &= status;
    if (verbose || !status)
    {
      auto& output = !status ? std::cerr : std::cout;
      elle::fprintf(output, "  [%s] %s:\n    [%s] network: %s",
                    result(status), volume.name,
                    result(status), volume.network);
      output << std::endl;
    }
  }
  std::cout << "Drives:" << std::endl;
  for (auto& elems: drives)
  {
    auto const& drive = elems.second.first;
    auto& status = elems.second.second;
    auto volume = volumes.find(drive.volume);
    auto volume_presents = volume != volumes.end();
    auto volume_ok = volume_presents && volume->second.second;
    auto network = networks.find(drive.network);
    auto network_presents = network != networks.end();
    auto network_ok = network_presents && network->second.second;
    status = network_ok && volume_ok;
    sane &= status;
    if (verbose || !status)
    {
      auto& output = !status ? std::cerr : std::cout;
      elle::fprintf(
        output, "  [%s] %s:\n    [%s] volume: %s\n    [%s] network: %s",
        result(status), drive.name,
        result(volume_ok), drive.volume,
        result(network_ok), drive.network
      );
      output << std::endl;
    }
  }
  return sane;
}

static
void
report_error(bool sane)
{
  if (!sane)
    throw elle::Error("Please check your configuration");
}

COMMAND(integrity)
{
  bool sane = true;
  sane &= _integrity(args, boost::none);
  report_error(sane);
}

COMMAND(networking)
{
  _networking(args, boost::none);
}

COMMAND(sanity)
{
  bool sane = true;
  sane &= _sanity(args, boost::none);
  report_error(sane);
}

COMMAND(run_all)
{
  bool sane = true;
  sane &= _sanity(args, boost::none);
  sane &= _integrity(args, boost::none);
  sane &= _networking(args, boost::none);
  report_error(sane);
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Mode::OptionDescription verbose =
   { "verbose,v", bool_switch(), "output everything" };

  Modes modes {
    {
      "all",
      "Perform all possible checks",
      &run_all,
      "",
      {
        verbose
      }
    },
    {
      "networking",
      "Perform networking checks",
      &networking,
      "",
      {
        verbose
      }
    },
    {
      "sanity",
      "Perform sanity checks",
      &sanity,
      "",
      {
        verbose
      }
    },
    {
      "integrity",
      "Perform integrity checks",
      &integrity,
      "",
      {
        verbose
      }
    }
  };
  return infinit::main("Infinit diagnostic utility", modes, argc, argv,
                       std::string("path"), boost::none);
}
