#include <infinit/cli/Volume.hh>

#include <infinit/MountOptions.hh>
#include <infinit/cli/Infinit.hh>
#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/doughnut/Local.hh>

#ifdef INFINIT_MACOSX
# include <reactor/network/reachability.hh>
# define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
# include <crash_reporting/gcc_fix.hh>
# include <CoreServices/CoreServices.h>
#endif

#ifdef INFINIT_WINDOWS
# include <fcntl.h>
# include <reactor/network/unix-domain-socket.hh>
# define IF_WINDOWS(Action) Action
#else
# define IF_WINDOWS(Action)
#endif

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

    namespace
    {
      using infinit::MountOptions;

      template <typename Symbol, typename T>
      void
      merge_(MountOptions& mo,
             Symbol sym, boost::optional<T> const& val,
             int = 0)
      {
        if (val)
          sym.attr_get(mo) = *val;
      }

      template <typename Symbol>
      void
      merge_(MountOptions& mo,
                 Symbol sym, Volume::Strings const& val,
                 int = 0)
      {
        if (!val.empty())
          Symbol::attr_get(mo).insert(Symbol::attr_get(mo).end(),
                                      val.begin(), val.end());
      }

      template <typename Symbol, typename T>
      void
      merge_(MountOptions& mo,
                 Symbol sym, const T& val,
                 int = 0)
      {
        sym.attr_get(mo) = val;
      }
    }

#define MERGE(Options, Symbol)                  \
    merge_(Options, cli::Symbol, Symbol)

#define MOUNT_OPTIONS_MERGE(Options)                                    \
    do {                                                                \
      namespace imo = infinit::mount_options;                           \
      merge_(Options, imo::fuse_options, fuse_option);                  \
      merge_(Options, imo::peers, peer);                                \
      MERGE(Options, mountpoint);                                       \
      merge_(Options, imo::as, cli.as_user().name);                     \
      MERGE(Options, readonly);                                         \
      MERGE(Options, fetch);                                            \
      merge_(Options, imo::push, push_endpoints);                       \
      MERGE(Options, cache);                                            \
      MERGE(Options, async);                                            \
      MERGE(Options, cache_ram_size);                                   \
      MERGE(Options, cache_ram_ttl);                                    \
      MERGE(Options, cache_ram_invalidation);                           \
      MERGE(Options, cache_disk_size);                                  \
      merge_(Options, imo::poll_beyond, fetch_endpoints_interval);      \
      if (listen)                                                       \
        merge_(Options, imo::listen_address,                            \
               boost::asio::ip::address::from_string(*listen));         \
      IF_WINDOWS(merge_(Options, imo::enable_monitoring, monitoring));  \
    } while (false)

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
                        // We don't seem to use these guys (port, etc.).
                        boost::optional<std::string> port_file,
                        boost::optional<int> port,
                        boost::optional<std::string> listen,
                        int fetch_endpoints_interval,
                        boost::optional<std::string> input)
    {
      ELLE_TRACE_SCOPE("create");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(volume_name, owner);
      auto network = ifnt.network_get(network_name, owner);

      // Normalize options *before* merging them into MountOptions.
      fetch |= fetch_endpoints;

      auto mo = infinit::MountOptions{};
      MOUNT_OPTIONS_MERGE(mo);

      if (default_permissions
          && *default_permissions != "r"
          && *default_permissions != "rw")
        elle::err("default-permissions must be 'r' or 'rw': %s",
                  *default_permissions);
      auto volume = infinit::Volume(name, network.name, mo, default_permissions,
                                    description);
      if (output_name)
      {
        auto output = cli.get_output(output_name);
        auto s = elle::serialization::json::SerializerOut(*output, false);
        s.serialize_forward(volume);
      }
      else
      {
        if (create_root || register_service)
        {
          auto model = network.run(owner, mo, false, monitoring,
                                   cli.compatibility_version());
          if (register_service)
          {
            ELLE_LOG_SCOPE("register volume in the network");
            model.first->service_add("volumes", name, volume);
          }
          if (create_root)
          {
            ELLE_LOG_SCOPE("create root directory");
            // Work around clang 7.0.2 bug.
            auto clang_model = std::shared_ptr<infinit::model::Model>(
              static_cast<infinit::model::Model*>(model.first.release()));
            auto fs = std::make_unique<infinit::filesystem::FileSystem>(
              infinit::filesystem::model = std::move(clang_model),
              infinit::filesystem::volume_name = name,
              infinit::filesystem::allow_root_creation = true);
            struct stat s;
            fs->path("/")->stat(&s);
          }
        }
        ifnt.volume_save(volume);
        cli.report_created("volume", name);
      }
      if (push || push_volume)
        ifnt.beyond_push("volume", name, volume, owner);
    }
  }
}
