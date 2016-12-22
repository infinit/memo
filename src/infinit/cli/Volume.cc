#include <infinit/cli/Volume.hh>

#include <infinit/cli/Infinit.hh>

ELLE_LOG_COMPONENT("cli.volume");

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Volume::Volume(Infinit& infinit)
      : Entity(infinit)
      , create(
        "Create a volume",
        das::cli::Options(),
        this->bind(modes::mode_create,
                   cli::name,
                   cli::network,
                   cli::description = boost::none,
                   cli::create_root = false,
                   cli::push_volume = false,
                   cli::output = boost::none,
                   cli::default_permissions = boost::none,
                   cli::register_service = false,
                   cli::allow_root_creation = false,
                   cli::mountpoint = boost::none,
                   cli::readonly = false,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                   cli::mount_name = boost::none,
#endif
#ifdef INFINIT_MACOSX
                   cli::mount_icon = boost::none,
                   cli::finder_sidebar = false,
#endif
                   cli::async = false,
#ifndef INFINIT_WINDOWS
                   cli::daemon = false,
#endif
                   cli::monitoring = true,
                   cli::fuse_option = Strings{},
                   cli::cache = false,
                   cli::cache_ram_size = boost::none,
                   cli::cache_ram_ttl = boost::none,
                   cli::cache_ram_invalidation = boost::none,
                   cli::cache_disk_size = boost::none,
                   cli::fetch_endpoints = false,
                   cli::fetch = false,
                   cli::peer = Strings{},
                   cli::peers_file = boost::none,
                   cli::push_endpoints = false,
                   cli::push = false,
                   cli::publish = false,
                   cli::advertise_host = Strings{},
                   cli::endpoints_file = boost::none,
                   cli::port_file = boost::none,
                   cli::port = boost::none,
                   cli::listen = boost::none,
                   cli::fetch_endpoints_interval = 300,
                   cli::input = boost::none
                   ))
    {}

    /*---------------.
    | Mode: create.  |
    `---------------*/

    void
    Volume::mode_create(std::string const& volume_name,
                        std::string const& network_name,
                        boost::optional<std::string> description,
                        bool create_root,
                        bool push_volume,
                        boost::optional<std::string> output_name,
                        boost::optional<std::string> default_permissions,
                        bool register_service,
                        bool allow_root_creation,
                        boost::optional<std::string> mountpoint,
                        bool readonly,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                        boost::optional<std::string> mount_name,
#endif
#ifdef INFINIT_MACOSX
                        boost::optional<std::string> mount_icon,
                        bool finder_sidebar,
#endif
                        bool async,
#ifndef INFINIT_WINDOWS
                        bool daemon,
#endif
                        bool monitoring,
                        Strings fuse_option,
                        bool cache,
                        boost::optional<int> cache_ram_size,
                        boost::optional<int> cache_ram_ttl,
                        boost::optional<int> cache_ram_invalidation,
                        boost::optional<int> cache_disk_size,
                        bool fetch_endpoints,
                        bool fetch,
                        Strings peer,
                        boost::optional<std::string> peers_file,
                        bool push_endpoints,
                        bool push,
                        bool publish,
                        Strings advertise_host,
                        boost::optional<std::string> endpoints_file,
                        boost::optional<std::string> port_file,
                        boost::optional<std::string> port,
                        boost::optional<std::string> listen,
                        int fetch_endpoints_interval,
                        boost::optional<std::string> input)
    {
      ELLE_TRACE_SCOPE("create");
    }
  }
}
