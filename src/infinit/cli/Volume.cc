#include <infinit/cli/Volume.hh>

#include <elle/Exit.hh>

#include <infinit/MountOptions.hh>
#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>
#include <infinit/cli/xattrs.hh>
#include <infinit/filesystem/filesystem.hh>
#include <infinit/grpc/grpc.hh>
#include <infinit/model/Model.hh>
#include <infinit/model/doughnut/Local.hh>

#ifdef INFINIT_MACOSX
# include <elle/reactor/network/reachability.hh>
# define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
# include <CoreServices/CoreServices.h>
#endif

#ifndef INFINIT_WINDOWS
# include <elle/reactor/network/unix-domain-socket.hh>
# define IF_NOT_WINDOWS(Action) Action
#else
# include <fcntl.h>
# define IF_NOT_WINDOWS(Action)
#endif

ELLE_LOG_COMPONENT("cli.volume");

#ifdef INFINIT_MACOSX
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wdeprecated-declarations"
namespace
{
  void
  add_path_to_finder_sidebar(std::string const& path)
  {
    ELLE_DUMP("add to sidebar: %s", path);
    LSSharedFileListRef favorite_items =
      LSSharedFileListCreate(NULL, kLSSharedFileListFavoriteItems, NULL);
    if (!favorite_items)
      return;
    CFStringRef path_str = CFStringCreateWithCString(
      kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
    CFArrayRef items_array = LSSharedFileListCopySnapshot(favorite_items, NULL);
    CFIndex count = CFArrayGetCount(items_array);
    bool in_list = false;
    for (CFIndex i = 0; i < count; i++)
    {
      LSSharedFileListItemRef item_ref
        = (LSSharedFileListItemRef)CFArrayGetValueAtIndex(items_array, i);
      CFURLRef item_url;
      OSStatus err = LSSharedFileListItemResolve(
        item_ref,
        kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes,
        &item_url,
        NULL);
      if (err == noErr && item_url)
      {
        if (CFStringRef item_path = CFURLCopyPath(item_url))
        {
          if (CFStringHasPrefix(item_path, path_str))
          {
            ELLE_DEBUG("already in sidebar favorites: %s", path);
            in_list = true;
          }
          CFRelease(item_path);
        }
        CFRelease(item_url);
      }
    }
    if (items_array)
      CFRelease(items_array);
    if (path_str)
      CFRelease(path_str);
    if (!in_list)
    {
      CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const unsigned char*>(path.data()),
        path.size(),
        true);
      LSSharedFileListItemRef item = LSSharedFileListInsertItemURL(
        favorite_items, kLSSharedFileListItemLast, NULL, NULL, url, NULL, NULL);
      ELLE_DEBUG("added to sidebar favorites: %s", path);
      if (url)
        CFRelease(url);
      if (item)
        CFRelease(item);
    }
    if (favorite_items)
      CFRelease(favorite_items);
  }

  void
  remove_path_from_finder_sidebar(std::string const& path)
  {
    ELLE_DUMP("remove from sidebar: %s", path);
    LSSharedFileListRef favorite_items =
      LSSharedFileListCreate(NULL, kLSSharedFileListFavoriteItems, NULL);
    if (!favorite_items)
      return;
    CFStringRef path_str = CFStringCreateWithCString(
      kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
    CFArrayRef items_array = LSSharedFileListCopySnapshot(favorite_items, NULL);
    CFIndex count = CFArrayGetCount(items_array);
    for (CFIndex i = 0; i < count; i++)
    {
      LSSharedFileListItemRef item_ref
        = (LSSharedFileListItemRef)CFArrayGetValueAtIndex(items_array, i);
      CFURLRef item_url;
      OSStatus err = LSSharedFileListItemResolve(
        item_ref,
        kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes,
        &item_url,
        NULL);
      if (err == noErr && item_url)
      {
        if (CFStringRef item_path = CFURLCopyPath(item_url))
        {
          if (CFStringHasPrefix(item_path, path_str))
          {
            LSSharedFileListItemRemove(favorite_items, item_ref);
            ELLE_DEBUG("found and removed item from sidebar: %s", path);
          }
          CFRelease(item_path);
        }
        CFRelease(item_url);
      }
    }
    if (items_array)
      CFRelease(items_array);
    if (path_str)
      CFRelease(path_str);
    if (favorite_items)
      CFRelease(favorite_items);
  }
}
# pragma clang diagnostic pop
#endif

namespace infinit
{
  namespace cli
  {
    namespace bfs = boost::filesystem;

    Volume::Volume(Infinit& infinit)
      : Object(infinit)
      , create(*this,
               "Create a volume",
               cli::name,
               cli::network,
               cli::description = boost::optional<std::string>(),
               cli::create_root = false,
               cli::push_volume = elle::defaulted(false),
               cli::output = boost::optional<std::string>(),
               cli::default_permissions = boost::optional<std::string>(),
               cli::register_service = false,
               cli::allow_root_creation = false,
               cli::mountpoint = boost::optional<std::string>(),
               cli::readonly = elle::defaulted(false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
               cli::mount_name = boost::optional<std::string>(),
#endif
#if defined INFINIT_MACOSX
               cli::mount_icon = boost::optional<std::string>(),
               cli::finder_sidebar = false,
#endif
               cli::async = elle::defaulted(false),
#if ! defined INFINIT_WINDOWS
               cli::daemon = false,
#endif
               cli::monitoring = elle::defaulted(true),
               cli::fuse_option = elle::defaulted(Strings{}),
               cli::cache = elle::defaulted(false),
               cli::cache_ram_size = boost::optional<int>(),
               cli::cache_ram_ttl = boost::optional<int>(),
               cli::cache_ram_invalidation = boost::optional<int>(),
               cli::cache_disk_size = boost::optional<uint64_t>(),
               cli::fetch_endpoints = elle::defaulted(false),
               cli::fetch = elle::defaulted(false),
               cli::peer = elle::defaulted(Strings{}),
               cli::push_endpoints = elle::defaulted(false),
               cli::push = elle::defaulted(false),
               cli::publish = elle::defaulted(false),
               cli::advertise_host = Strings{},
               cli::endpoints_file = boost::optional<std::string>(),
               cli::port_file = boost::optional<std::string>(),
               cli::port = boost::optional<int>(),
               cli::listen = boost::optional<std::string>(),
               cli::fetch_endpoints_interval = elle::defaulted(300),
               cli::input = boost::optional<std::string>(),
               cli::block_size = elle::defaulted(1024 * 1024))
      , delete_(*this,
                "Delete a volume locally",
                cli::name,
                cli::pull = false,
                cli::purge = false)
      , export_(*this,
                "Export a volume for someone else to import",
                cli::name,
                cli::output = boost::none)
      , fetch(*this,
              "Fetch a volume from {hub}",
              cli::name = boost::none,
              cli::network = boost::none,
              cli::service = false)
      , import(*this,
               "Import a volume",
               cli::input = boost::none,
               cli::mountpoint = boost::none)
      , list(*this, "List volumes")
        // FIXME: Same options as run, large duplication.
      , mount(*this,
              "Mount a volume",
              cli::name,
              cli::allow_root_creation = false,
              cli::mountpoint = boost::optional<std::string>(),
              cli::readonly = elle::defaulted(false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
              cli::mount_name = boost::optional<std::string>(),
#endif
#ifdef INFINIT_MACOSX
              cli::mount_icon = boost::optional<std::string>(),
              cli::finder_sidebar = false,
#endif
              cli::async = elle::defaulted(false),
#ifndef INFINIT_WINDOWS
              cli::daemon = false,
#endif
              cli::monitoring = elle::defaulted(true),
              cli::fuse_option = elle::defaulted(Strings{}),
              cli::cache = elle::defaulted(false),
              cli::cache_ram_size = boost::optional<int>(),
              cli::cache_ram_ttl = boost::optional<int>(),
              cli::cache_ram_invalidation = boost::optional<int>(),
              cli::cache_disk_size = boost::optional<uint64_t>(),
              cli::fetch_endpoints = elle::defaulted(false),
              cli::fetch = elle::defaulted(false),
              cli::peer = elle::defaulted(Strings{}),
              cli::peers_file = boost::optional<std::string>(),
              cli::push_endpoints = elle::defaulted(false),
              cli::register_service = false,
              cli::no_local_endpoints = false,
              cli::no_public_endpoints = false,
              cli::push = elle::defaulted(false),
              cli::map_other_permissions = true,
              cli::publish = elle::defaulted(false),
              cli::advertise_host = Strings{},
              cli::endpoints_file = boost::optional<std::string>(),
              cli::port_file = boost::optional<std::string>(),
              cli::port = boost::optional<int>(),
              cli::listen = boost::optional<std::string>(),
              cli::fetch_endpoints_interval = elle::defaulted(300),
              cli::input = boost::optional<std::string>(),
              cli::disable_UTF_8_conversion = false,
              cli::grpc = boost::optional<std::string>()
#if INFINIT_ENABLE_PROMETHEUS
              , cli::prometheus = boost::optional<std::string>()
#endif
        )
      , pull(*this,
             "Remove a volume from {hub}",
             cli::name,
             cli::purge = false)
      , push(*this,
             "Push a volume to {hub}",
             cli::name)
      , run(*this,
            "Run a volume",
            cli::name,
            cli::allow_root_creation = false,
            cli::mountpoint = boost::optional<std::string>(),
            cli::readonly = elle::defaulted(false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
            cli::mount_name = boost::optional<std::string>(),
#endif
#ifdef INFINIT_MACOSX
            cli::mount_icon = boost::optional<std::string>(),
            cli::finder_sidebar = false,
#endif
            cli::async = elle::defaulted(false),
#ifndef INFINIT_WINDOWS
            cli::daemon = false,
#endif
            cli::monitoring = elle::defaulted(true),
            cli::fuse_option = elle::defaulted(Strings{}),
            cli::cache = elle::defaulted(false),
            cli::cache_ram_size = boost::optional<int>(),
            cli::cache_ram_ttl = boost::optional<int>(),
            cli::cache_ram_invalidation = boost::optional<int>(),
            cli::cache_disk_size = boost::optional<uint64_t>(),
            cli::fetch_endpoints = elle::defaulted(false),
            cli::fetch = elle::defaulted(false),
            cli::peer = elle::defaulted(Strings{}),
            cli::peers_file = boost::optional<std::string>(),
            cli::push_endpoints = elle::defaulted(false),
            cli::register_service = false,
            cli::no_local_endpoints = false,
            cli::no_public_endpoints = false,
            cli::push = elle::defaulted(false),
            cli::map_other_permissions = true,
            cli::publish = elle::defaulted(false),
            cli::advertise_host = Strings{},
            cli::endpoints_file = boost::optional<std::string>(),
            cli::port_file = boost::optional<std::string>(),
            cli::port = boost::optional<int>(),
            cli::listen = boost::optional<std::string>(),
            cli::fetch_endpoints_interval = elle::defaulted(300),
            cli::input = boost::optional<std::string>(),
            cli::disable_UTF_8_conversion = false,
            cli::grpc = boost::optional<std::string>()
#if INFINIT_ENABLE_PROMETHEUS
              , cli::prometheus = boost::optional<std::string>()
#endif
      )
#if !defined INFINIT_WINDOWS
      , start(*this,
              "Start a volume through the daemon",
              cli::name,
              cli::allow_root_creation = false,
              cli::mountpoint = boost::optional<std::string>(),
              cli::readonly = elle::defaulted(false),
# if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
              cli::mount_name = boost::optional<std::string>(),
# endif
# ifdef INFINIT_MACOSX
              cli::mount_icon = boost::optional<std::string>(),
              cli::finder_sidebar = false,
# endif
              cli::async = elle::defaulted(false),
# ifndef INFINIT_WINDOWS
              cli::daemon = false,
# endif
              cli::monitoring = elle::defaulted(true),
              cli::fuse_option = elle::defaulted(Strings{}),
              cli::cache = elle::defaulted(false),
              cli::cache_ram_size = boost::optional<int>(),
              cli::cache_ram_ttl = boost::optional<int>(),
              cli::cache_ram_invalidation = boost::optional<int>(),
              cli::cache_disk_size = boost::optional<uint64_t>(),
              cli::fetch_endpoints = elle::defaulted(false),
              cli::fetch = elle::defaulted(false),
              cli::peer = elle::defaulted(Strings{}),
              cli::push_endpoints = elle::defaulted(false),
              cli::register_service = false,
              cli::no_local_endpoints = false,
              cli::no_public_endpoints = false,
              cli::push = elle::defaulted(false),
              cli::map_other_permissions = true,
              cli::publish = elle::defaulted(false),
              cli::advertise_host = Strings{},
              cli::endpoints_file = boost::optional<std::string>(),
              cli::port_file = boost::optional<std::string>(),
              cli::port = boost::optional<int>(),
              cli::listen = boost::optional<std::string>(),
              cli::fetch_endpoints_interval = elle::defaulted(300),
              cli::input = boost::optional<std::string>())
      , status(*this,
               "Get volume status",
               cli::name)
      , stop(*this,
             "Stop a volume",
             cli::name)
#endif
      , update(*this,
               "Update a volume with default run options",
               cli::name,
               cli::description = boost::optional<std::string>(),
               cli::allow_root_creation = false,
               cli::mountpoint = boost::optional<std::string>(),
               cli::readonly = elle::defaulted(false),
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
               cli::mount_name = boost::optional<std::string>(),
#endif
#ifdef INFINIT_MACOSX
               cli::mount_icon = boost::optional<std::string>(),
               cli::finder_sidebar = false,
#endif
               cli::async = elle::defaulted(false),
#ifndef INFINIT_WINDOWS
               cli::daemon = false,
#endif
               cli::monitoring = elle::defaulted(true),
               cli::fuse_option = elle::defaulted(Strings{}),
               cli::cache = elle::defaulted(false),
               cli::cache_ram_size = boost::optional<int>(),
               cli::cache_ram_ttl = boost::optional<int>(),
               cli::cache_ram_invalidation = boost::optional<int>(),
               cli::cache_disk_size = boost::optional<uint64_t>(),
               cli::fetch_endpoints = elle::defaulted(false),
               cli::fetch = elle::defaulted(false),
               cli::peer = elle::defaulted(Strings{}),
               cli::push_endpoints = elle::defaulted(false),
               cli::push = elle::defaulted(false),
               cli::map_other_permissions = true,
               cli::publish = elle::defaulted(false),
               cli::advertise_host = Strings{},
               cli::endpoints_file = boost::optional<std::string>(),
               cli::port_file = boost::optional<std::string>(),
               cli::port = boost::optional<int>(),
               cli::listen = boost::optional<std::string>(),
               cli::fetch_endpoints_interval = elle::defaulted(300),
               cli::input = boost::optional<std::string>(),
               cli::user = boost::optional<std::string>(),
               cli::block_size = boost::optional<int>())
      , syscall(infinit)
    {}

    namespace
    {
      /// These two arguments are aliases.  Make them consistent.
      void resolve_aliases(elle::Defaulted<bool>& arg1,
                           elle::Defaulted<bool>& arg2)
      {
        if (arg1 && !arg2)
          arg2 = *arg1;
        else if (!arg1 && arg2)
          arg1 = *arg2;
        assert(*arg1 == *arg2);
      }
    }

    /*---------------.
    | Mode: create.  |
    `---------------*/

    namespace
    {
      using infinit::MountOptions;

#ifdef INFINIT_MACOSX
      void
      emplace_back(boost::optional<Volume::Strings>& ss,
                   std::string s)
      {
        if (!ss)
          ss = Volume::Strings{};
        ss->emplace_back(std::move(s));
      }
#endif

      template <typename Symbol, typename T>
      void
      merge_(MountOptions& mo,
             Symbol sym, const T& val)
      {
        sym.attr_get(mo) = val;
      }

      template <typename Symbol>
      void
      merge_(MountOptions& mo,
             Symbol sym, Volume::Strings const& val)
      {
        auto& m = Symbol::attr_get(mo);
        if (!m)
          m = Volume::Strings{};
        m->insert(m->end(), val.begin(), val.end());
      }

      template <typename Symbol, typename T>
      void
      merge_(MountOptions& mo,
             Symbol sym, boost::optional<T> const& val)
      {
        if (val)
          merge_(mo, sym, *val);
      }

      template <typename Symbol, typename T>
      void
      merge_(MountOptions& mo,
             Symbol sym, elle::Defaulted<T> const& val)
      {
        if (val)
          merge_(mo, sym, *val);
      }
    }

#define MERGE(Options, Symbol)                  \
    merge_(Options, cli::Symbol, Symbol)

    // FIXME: We do not merge 'as' nor 'user', although we used to do
    // it in the legacy CLI.  However, this appears to have been dead
    // code in the legacy version: the 'args' that were passed did not
    // include --as's argument, neither --user's one.  Observation
    // based on the reading the code, and on running the test-suite.
    //
    // merge_(Options, imo::as, cli.as());
#define MOUNT_OPTIONS_MERGE(Options)                                    \
    do {                                                                \
      namespace imo = infinit::mount_options;                           \
      merge_(Options, imo::fuse_options, fuse_option);                  \
      merge_(Options, imo::peers, peer);                                \
      MERGE(Options, mountpoint);                                       \
      MERGE(Options, readonly);                                         \
      MERGE(Options, fetch);                                            \
      MERGE(Options, push);                                             \
      MERGE(Options, publish);                                          \
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
      IF_NOT_WINDOWS(merge_(Options, imo::enable_monitoring, monitoring));  \
    } while (false)

    void
    Volume::mode_create(std::string const& volume_name,
                        std::string const& network_name,
                        boost::optional<std::string> description,
                        bool create_root,
                        Defaulted<bool> push_volume,
                        boost::optional<std::string> output_name,
                        boost::optional<std::string> default_permissions,
                        bool register_service,
                        bool allow_root_creation,
                        boost::optional<std::string> mountpoint,
                        Defaulted<bool> readonly,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                        boost::optional<std::string> mount_name,
#endif
#ifdef INFINIT_MACOSX
                        boost::optional<std::string> mount_icon,
                        bool finder_sidebar,
#endif
                        Defaulted<bool> async,
#ifndef INFINIT_WINDOWS
                        bool daemon,
#endif
                        Defaulted<bool> monitoring,
                        Defaulted<Strings> fuse_option,
                        Defaulted<bool> cache,
                        boost::optional<int> cache_ram_size,
                        boost::optional<int> cache_ram_ttl,
                        boost::optional<int> cache_ram_invalidation,
                        boost::optional<uint64_t> cache_disk_size,
                        Defaulted<bool> fetch_endpoints,
                        Defaulted<bool> fetch,
                        Defaulted<Strings> peer,
                        Defaulted<bool> push_endpoints,
                        Defaulted<bool> push,
                        Defaulted<bool> publish,
                        Strings advertise_host,
                        boost::optional<std::string> endpoints_file,
                        // We don't seem to use these guys (port, etc.).
                        boost::optional<std::string> port_file,
                        boost::optional<int> port,
                        boost::optional<std::string> listen,
                        Defaulted<int> fetch_endpoints_interval,
                        boost::optional<std::string> input,
                        Defaulted<int> block_size)
    {
      ELLE_TRACE_SCOPE("create");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(volume_name, owner);
      auto network = ifnt.network_get(network_name, owner);

      // Normalize options *before* merging them into MountOptions.
      resolve_aliases(fetch, fetch_endpoints);
      resolve_aliases(push, push_endpoints);

      auto mo = infinit::MountOptions{};
      MOUNT_OPTIONS_MERGE(mo);

      if (default_permissions
          && *default_permissions != "r"
          && *default_permissions != "rw")
        elle::err("default-permissions must be 'r' or 'rw': %s",
                  *default_permissions);
      auto volume = infinit::Volume(
        name,
        network.name,
        mo,
        default_permissions,
        description,
        block_size ?
          boost::optional<int>(block_size.get()) : boost::optional<int>());
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
          auto model = network.run(owner, mo, false, *monitoring,
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
      }
      if (*push || *push_volume)
        ifnt.beyond_push("volume", name, volume, owner);
    }



    /*---------------.
    | Mode: delete.  |
    `---------------*/

    void
    Volume::mode_delete(std::string const& volume_name,
                        bool pull,
                        bool purge)
    {
      ELLE_TRACE_SCOPE("delete");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(volume_name, owner);
      auto volume = ifnt.volume_get(name);
      if (purge)
        for (auto const& drive: ifnt.drives_for_volume(name))
          ifnt.drive_delete(drive);
      if (pull)
        ifnt.beyond_delete("volume", name, owner, true, purge);
      ifnt.volume_delete(volume);
    }

    /*---------------.
    | Mode: export.  |
    `---------------*/

    void
    Volume::mode_export(std::string const& volume_name,
                        boost::optional<std::string> const& output_name)
    {
      ELLE_TRACE_SCOPE("export");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(volume_name, owner);
      auto volume = ifnt.volume_get(name);
      auto output = cli.get_output(output_name);
      volume.mount_options.mountpoint.reset();
      {
        auto s = elle::serialization::json::SerializerOut(*output, false);
        s.serialize_forward(volume);
      }
      cli.report_exported(*output, "volume", volume.name);
    }

    /*--------------.
    | Mode: fetch.  |
    `--------------*/

    void
    Volume::mode_fetch(boost::optional<std::string> volume_name,
                       boost::optional<std::string> network_name,
                       bool service)
    {
      ELLE_TRACE_SCOPE("fetch");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      using VolumesMap
        = std::unordered_map<std::string, std::vector<infinit::Volume>>;
      if (service)
      {
        if (!network_name)
          elle::err<CLIError>("--network is mandatory with --service");
        auto net_name = ifnt.qualified_name(*network_name, owner);
        auto network = ifnt.network_get(net_name, owner);
        auto dht = network.run(owner);
        auto services = dht->services();
        auto volumes = services.find("volumes");
        if (volumes != services.end())
          for (auto volume: volumes->second)
            if ((!volume_name
                 || ifnt.qualified_name(*volume_name, owner) == volume.first)
                && !ifnt.volume_has(volume.first))
            {
              auto v = elle::serialization::binary::deserialize<infinit::Volume>
                (dht->fetch(volume.second)->data());
              ifnt.volume_save(v, true);
            }
      }
      else if (volume_name)
      {
        auto name = ifnt.qualified_name(*volume_name, owner);
        auto desc = ifnt.beyond_fetch<infinit::Volume>("volume", name);
        ifnt.volume_save(std::move(desc), true);
      }
      else if (network_name)
      {
        // Fetch all volumes for network.
        auto net_name = ifnt.qualified_name(*network_name, owner);
        auto res = ifnt.beyond_fetch<VolumesMap>(
            elle::sprintf("networks/%s/volumes", net_name),
            "volumes for network",
            net_name);
        for (auto const& volume: res["volumes"])
          ifnt.volume_save(std::move(volume), true);
      }
      else
      {
        // Fetch all volumes for owner.
        auto res = ifnt.beyond_fetch<VolumesMap>(
            elle::sprintf("users/%s/volumes", owner.name),
            "volumes for user",
            owner.name,
            owner);
        for (auto const& v: res["volumes"])
          ifnt.volume_save(v, true);
      }
    }

    /*---------------.
    | Mode: import.  |
    `---------------*/

    void
    Volume::mode_import(boost::optional<std::string> input_name,
                        boost::optional<std::string> mountpoint)
    {
      ELLE_TRACE_SCOPE("import");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto input = this->cli().get_input(input_name);
      auto s = elle::serialization::json::SerializerIn(*input, false);
      auto volume = infinit::Volume(s);
      volume.mount_options.mountpoint = mountpoint;
      ifnt.volume_save(volume);
      cli.report_imported("volume", volume.name);
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/

    void
    Volume::mode_list()
    {
      ELLE_TRACE_SCOPE("list");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();

      if (elle::os::getenv("INFINIT_CRASH", false))
        *(volatile int*)nullptr = 0;

      if (cli.script())
      {
        auto l = elle::json::make_array(ifnt.volumes_get(), [&](auto const& volume) {
          auto res = elle::json::Object
            {
              {"name", static_cast<std::string>(volume.name)},
              {"network", volume.network},
            };
          if (volume.mount_options.mountpoint)
            res["mountpoint"] = *volume.mount_options.mountpoint;
          if (volume.description)
            res["description"] = *volume.description;
          return res;
          });
        elle::json::write(std::cout, l);
      }
      else
        for (auto const& volume: ifnt.volumes_get())
        {
          std::cout << volume.name;
          if (volume.description)
            std::cout << " \"" << volume.description.get() << "\"";
          std::cout << ": network " << volume.network;
          if (volume.mount_options.mountpoint)
            std::cout << " on " << *volume.mount_options.mountpoint;
          std::cout << std::endl;
        }
    }

    /*--------------.
    | Mode: mount.  |
    `--------------*/

    void
    Volume::mode_mount(std::string const& volume_name,
                       bool allow_root_creation,
                       boost::optional<std::string> mountpoint,
                       Defaulted<bool> readonly,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                       boost::optional<std::string> mount_name,
#endif
#ifdef INFINIT_MACOSX
                       boost::optional<std::string> mount_icon,
                       bool finder_sidebar,
#endif
                       Defaulted<bool> async,
#ifndef INFINIT_WINDOWS
                       bool daemon,
#endif
                       Defaulted<bool> monitoring,
                       Defaulted<Strings> fuse_option,
                       Defaulted<bool> cache,
                       boost::optional<int> cache_ram_size,
                       boost::optional<int> cache_ram_ttl,
                       boost::optional<int> cache_ram_invalidation,
                       boost::optional<uint64_t> cache_disk_size,
                       Defaulted<bool> fetch_endpoints,
                       Defaulted<bool> fetch,
                       Defaulted<Strings> peer,
                       boost::optional<std::string> peers_file,
                       Defaulted<bool> push_endpoints,
                       bool register_service,
                       bool no_local_endpoints,
                       bool no_public_endpoints,
                       Defaulted<bool> push,
                       bool map_other_permissions,
                       Defaulted<bool> publish,
                       Strings advertise_host,
                       boost::optional<std::string> endpoints_file,
                       boost::optional<std::string> port_file,
                       boost::optional<int> port,
                       boost::optional<std::string> listen,
                       Defaulted<int> fetch_endpoints_interval,
                       boost::optional<std::string> input_name,
                       bool disable_UTF_8_conversion,
                       boost::optional<std::string> grpc
#if INFINIT_ENABLE_PROMETHEUS
                     , boost::optional<std::string> prometheus
#endif
                       )
    {
      ELLE_TRACE_SCOPE("mount");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();

      if (!mountpoint)
      {
        auto owner = cli.as_user();
        auto const name = ifnt.qualified_name(volume_name, owner);
        auto volume = ifnt.volume_get(name);
        if (!volume.mount_options.mountpoint)
          elle::err<CLIError>("option --mountpoint is needed");
      }
      mode_run(volume_name,
               allow_root_creation,
               mountpoint,
               readonly,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
               mount_name,
#endif
#ifdef INFINIT_MACOSX
               mount_icon,
               finder_sidebar,
#endif
               async,
#ifndef INFINIT_WINDOWS
               daemon,
#endif
               monitoring,
               fuse_option,
               cache,
               cache_ram_size,
               cache_ram_ttl,
               cache_ram_invalidation,
               cache_disk_size,
               fetch_endpoints,
               fetch,
               peer,
               peers_file,
               push_endpoints,
               register_service,
               no_local_endpoints,
               no_public_endpoints,
               push,
               map_other_permissions,
               publish,
               advertise_host,
               endpoints_file,
               port_file,
               port,
               listen,
               fetch_endpoints_interval,
               input_name,
               disable_UTF_8_conversion,
               grpc
#if INFINIT_ENABLE_PROMETHEUS
               , prometheus
#endif
               );
    }


    /*-------------.
    | Mode: pull.  |
    `-------------*/

    void
    Volume::mode_pull(std::string const& volume_name,
                     bool purge)
    {
      ELLE_TRACE_SCOPE("pull");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto const name = ifnt.qualified_name(volume_name, owner);
      ifnt.beyond_delete("volume", name, owner, false, purge);
    }


    /*-------------.
    | Mode: push.  |
    `-------------*/

    void
    Volume::mode_push(std::string const& volume_name)
    {
      ELLE_TRACE_SCOPE("push");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto const name = ifnt.qualified_name(volume_name, owner);
      auto volume = ifnt.volume_get(name);
      // Don't push the mountpoint to beyond.
      volume.mount_options.mountpoint = boost::none;
      auto network = ifnt.network_get(volume.network, owner);
      auto owner_uid = infinit::User::uid(*network.dht()->owner);
      ifnt.beyond_push("volume", name, volume, owner);

    }


    /*------------.
    | Mode: run.  |
    `------------*/

    void
    Volume::mode_run(std::string const& volume_name,
                     bool allow_root_creation,
                     boost::optional<std::string> mountpoint,
                     Defaulted<bool> readonly,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                     boost::optional<std::string> mount_name,
#endif
#ifdef INFINIT_MACOSX
                     boost::optional<std::string> mount_icon,
                     bool finder_sidebar,
#endif
                     Defaulted<bool> async,
#ifndef INFINIT_WINDOWS
                     bool daemon,
#endif
                     Defaulted<bool> monitoring,
                     Defaulted<Strings> fuse_option,
                     Defaulted<bool> cache,
                     boost::optional<int> cache_ram_size,
                     boost::optional<int> cache_ram_ttl,
                     boost::optional<int> cache_ram_invalidation,
                     boost::optional<uint64_t> cache_disk_size,
                     Defaulted<bool> fetch_endpoints,
                     Defaulted<bool> fetch,
                     Defaulted<Strings> peer,
                     boost::optional<std::string> peers_file,
                     Defaulted<bool> push_endpoints,
                     bool register_service,
                     bool no_local_endpoints,
                     bool no_public_endpoints,
                     Defaulted<bool> push,
                     bool map_other_permissions,
                     Defaulted<bool> publish,
                     Strings advertise_host,
                     boost::optional<std::string> endpoints_file,
                     boost::optional<std::string> port_file,
                     boost::optional<int> port,
                     boost::optional<std::string> listen,
                     Defaulted<int> fetch_endpoints_interval,
                     boost::optional<std::string> input_name,
                     bool disable_UTF_8_conversion,
                     boost::optional<std::string> grpc
#if INFINIT_ENABLE_PROMETHEUS
                     , boost::optional<std::string> prometheus
#endif
                     )
    {
      ELLE_TRACE_SCOPE("run");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      // Normalize options *before* merging them into MountOptions.
      resolve_aliases(fetch, fetch_endpoints);
      resolve_aliases(push, push_endpoints);

      auto const name = ifnt.qualified_name(volume_name, owner);
      auto volume = ifnt.volume_get(name);
      auto& mo = volume.mount_options;
      MOUNT_OPTIONS_MERGE(mo);
#ifdef INFINIT_MACOSX
      if (mo.mountpoint && !disable_UTF_8_conversion)
        emplace_back(mo.fuse_options,
                     "modules=iconv,from_code=UTF-8,to_code=UTF-8-MAC");
#endif
      bool created_mountpoint = false;
      if (mo.mountpoint)
      {
#ifdef INFINIT_WINDOWS
        if (mo.mountpoint->size() != 2 || (*mo.mountpoint)[1] != ':')
#elif defined INFINIT_MACOSX
        // Do not try to create folder in /Volumes.
        auto mount_path = bfs::path(*mo.mountpoint);
        auto mount_parent
          = boost::algorithm::to_lower_copy(mount_path.parent_path().string());
        if (mount_parent.find("/volumes") != 0)
#endif
        try
        {
          if (bfs::exists(*mo.mountpoint))
          {
            if (!bfs::is_directory(*mo.mountpoint))
              elle::err("mountpoint is not a directory");
            if (!bfs::is_empty(*mo.mountpoint))
              elle::err("mountpoint is not empty");
          }
          created_mountpoint = bfs::create_directories(*mo.mountpoint);
        }
        catch (bfs::filesystem_error const& e)
        {
          elle::err("unable to access mountpoint: %s", e.what());
        }
      }
      if (mo.fuse_options && !mo.mountpoint)
        elle::err<CLIError>("FUSE options require the volume to be mounted");
      auto network = ifnt.network_get(volume.network, owner);
      network.ensure_allowed(owner, "run", "volume");
      ELLE_TRACE("run network");
#ifndef INFINIT_WINDOWS
      auto daemon_handle = daemon ? daemon_hold(0, 1) : daemon_invalid;
#endif
      cli.report_action("running", "network", network.name);
      auto model_and_threads = network.run(
        owner, mo, true, *monitoring,
        cli.compatibility_version(), port);
      auto model = std::move(model_and_threads.first);
      hook_stats_signals(*model);
      if (peers_file)
      {
        auto more_peers = hook_peer_discovery(*model, *peers_file);
        ELLE_TRACE("Peer list file got %s peers", more_peers.size());
        if (!more_peers.empty())
          model->overlay()->discover(more_peers);
      }
      if (register_service)
      {
        ELLE_LOG_SCOPE("register volume in the network");
        model->service_add("volumes", name, volume);
      }
      // Only push if we are contributing storage.
      bool push_p = mo.push.value_or(mo.publish.value_or(false)) &&
        model->local();
      auto local_endpoint = boost::optional<model::Endpoint>{};
      if (model->local())
      {
        local_endpoint = model->local()->server_endpoint();
        if (port_file)
          port_to_file(local_endpoint->port(), *port_file);
        if (endpoints_file)
          endpoints_to_file(model->local()->server_endpoints(),
                            *endpoints_file);
      }
      auto run = [&, push_p]
      {
        elle::reactor::Thread::unique_ptr stat_thread;
        if (push_p)
          stat_thread = network.make_stat_update_thread(ifnt, owner, *model);
        ELLE_TRACE_SCOPE("run volume");
        cli.report_action("running", "volume", volume.name);
        auto& dht = *model;
        auto fs = volume.run(std::move(model),
                             allow_root_creation,
                             map_other_permissions
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                             , mount_name
#endif
#ifdef INFINIT_MACOSX
                             , mount_icon
#endif
                             );
        if (grpc)
        {
          new elle::reactor::Thread("grpc", [&dht, fs=fs.get(), grpc] {
              infinit::grpc::serve_grpc(dht, *fs, *grpc);
          });
        }
#if INFINIT_ENABLE_PROMETHEUS
        if (prometheus)
          infinit::prometheus::endpoint(*prometheus);
#endif
        auto killer = cli.killed.connect([&, count = std::make_shared<int>(0)]
          {
            switch ((*count)++)
              {
              case 0:
                break;
              case 1:
                ELLE_LOG("already shutting down gracefully, "
                         "repeat to force filesystem operations termination");
                break;
              case 2:
                ELLE_LOG("force termination");
                fs->kill();
                break;
              default:
                ELLE_LOG("already forcing termination as hard as possible");
              }
          });
        // Experimental: poll root on mount to trigger caching.
#if 0
        auto root_poller = boost::optional<std::thread>{};
        if (mo.mountpoint && mo.cache && mo.cache.get())
          root_poller.emplace(
            [root = mo.mountpoint.get()]
            {
              try
              {
                bfs::status(root);
                for (auto const& p: bfs::directory_iterator(root))
                  continue;
              }
              catch (bfs::filesystem_error const& e)
              {
                ELLE_WARN("error polling root: %s", e);
              }
            });
        elle::SafeFinally root_poller_join(
          [&]
          {
            if (root_poller)
              root_poller->join();
          });
#endif
        if (volume.default_permissions && !volume.default_permissions->empty())
        {
          auto ops =
            dynamic_cast<infinit::filesystem::FileSystem*>(fs->operations().get());
          ops->on_root_block_create.connect([&] {
            ELLE_DEBUG("root_block hook triggered");
            auto path = fs->path("/");
            int mode = 0700;
            if (*volume.default_permissions == "rw")
              mode |= 06;
            else if (*volume.default_permissions == "r")
              mode |= 04;
            else
            {
              ELLE_WARN("Unexpected default permissions %s",
                        *volume.default_permissions);
              return;
            }
            path->chmod(mode);
            path->setxattr("infinit.auth.inherit", "1", 0);
          });
        }
#ifdef INFINIT_MACOSX
        if (finder_sidebar && mo.mountpoint)
        {
          auto mountpoint = mo.mountpoint;
          elle::reactor::background([mountpoint]
            {
              add_path_to_finder_sidebar(mountpoint.get());
            });
        }
        auto reachability = std::unique_ptr<elle::reactor::network::Reachability>{};
#endif
        elle::SafeFinally unmount([&]
        {
#ifdef INFINIT_MACOSX
          if (reachability)
            reachability->stop();
          if (finder_sidebar && mo.mountpoint)
          {
            auto mountpoint = mo.mountpoint;
            elle::reactor::background([mountpoint]
              {
                remove_path_from_finder_sidebar(mountpoint.get());
              });
          }
#endif
          ELLE_TRACE("unmounting")
            fs->unmount();
          if (created_mountpoint)
          {
            try
            {
              bfs::remove(mo.mountpoint.get());
            }
            catch (bfs::filesystem_error const&)
            {}
          }
        });
#ifdef INFINIT_MACOSX
        if (elle::os::getenv("INFINIT_LOG_REACHABILITY", true))
        {
          reachability.reset(new elle::reactor::network::Reachability(
            {},
            [&] (elle::reactor::network::Reachability::NetworkStatus status)
            {
              using NetworkStatus = elle::reactor::network::Reachability::NetworkStatus;
              if (status == NetworkStatus::Unreachable)
                ELLE_LOG("lost network connection");
              else
                ELLE_LOG("got network connection");
            },
            true));
          reachability->start();
        }
#endif
#ifndef INFINIT_WINDOWS
        if (daemon)
        {
          ELLE_TRACE("releasing daemon");
          daemon_release(daemon_handle);
        }
#endif
        if (cli.script())
        {
          auto input = commands_input(input_name);
          auto handles =
            std::unordered_map<std::string,
                               std::unique_ptr<elle::reactor::filesystem::Handle>>{};
          while (true)
          {
            std::string op;
            std::string pathname;
            std::string handlename;
            try
            {
              auto json =
                boost::any_cast<elle::json::Object>(elle::json::read(*input));
              ELLE_TRACE("got command: %s", json);
              auto command = elle::serialization::json::SerializerIn(json, false);
              op = command.deserialize<std::string>("operation");
              auto path = std::shared_ptr<elle::reactor::filesystem::Path>{};
              try
              {
                pathname = command.deserialize<std::string>("path");
                path = fs->path(pathname);
              }
              catch (...)
              {}
              try
              {
                handlename = command.deserialize<std::string>("handle");
              }
              catch (...)
              {}
              auto require_path = [&]
                {
                  if (!path)
                    throw elle::reactor::filesystem::Error(
                      ENOENT,
                      elle::sprintf("no such file or directory: %s", pathname));
                };
              if (op == "list_directory")
              {
                require_path();
                auto entries = std::vector<std::string>{};
                path->list_directory(
                  [&] (std::string const& path, struct stat*)
                  {
                    entries.push_back(path);
                  });
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("entries", entries);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("path", pathname);
                continue;
              }
              else if (op == "mkdir")
              {
                require_path();
                path->mkdir(0777);
              }
              else if (op == "stat")
              {
                require_path();
                struct stat st;
                path->stat(&st);
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("path", pathname);
                response.serialize("st_dev"    , st.st_dev            );
                response.serialize("st_ino"    , st.st_ino            );
                response.serialize("st_mode"   , st.st_mode           );
                response.serialize("st_nlink"  , st.st_nlink          );
                response.serialize("st_uid"    , st.st_uid            );
                response.serialize("st_gid"    , st.st_gid            );
                response.serialize("st_rdev"   , st.st_rdev           );
                response.serialize("st_size"   , uint64_t(st.st_size) );
#ifndef INFINIT_WINDOWS
                response.serialize("st_blksize", int64_t(st.st_blksize));
                response.serialize("st_blocks" , st.st_blocks);
#endif
                response.serialize("st_atime"  , uint64_t(st.st_atime));
                response.serialize("st_mtime"  , uint64_t(st.st_mtime));
                response.serialize("st_ctime"  , uint64_t(st.st_ctime));
                continue;
              }
              else if (op == "setxattr")
              {
                require_path();
                auto name = command.deserialize<std::string>("name");
                auto value = command.deserialize<std::string>("value");
                path->setxattr(name, value, 0);
              }
              else if (op == "getxattr")
              {
                require_path();
                auto name = command.deserialize<std::string>("name");
                auto value = path->getxattr(name);
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("value", value);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("path", pathname);
                continue;
              }
              else if (op == "listxattr")
              {
                require_path();
                auto attrs = path->listxattr();
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("entries", attrs);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("path", pathname);
                continue;
              }
              else if (op == "removexattr")
              {
                require_path();
                auto name = command.deserialize<std::string>("name");
                path->removexattr(name);
              }
              else if (op == "link")
              {
                require_path();
                auto target = command.deserialize<std::string>("target");
                path->link(target);
              }
              else if (op == "symlink")
              {
                require_path();
                auto target = command.deserialize<std::string>("target");
                path->symlink(target);
              }
              else if (op == "readlink")
              {
                require_path();
                auto res = path->readlink();
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("target", res.string());
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("path", pathname);
                continue;
              }
              else if (op == "rename")
              {
                require_path();
                auto target = command.deserialize<std::string>("target");
                path->rename(target);
              }
              else if (op == "truncate")
              {
                require_path();
                auto sz = command.deserialize<uint64_t>("size");
                path->truncate(sz);
              }
              else if (op == "utimens")
              {
                require_path();
                auto last_access = command.deserialize<uint64_t>("access");
                auto last_change = command.deserialize<uint64_t>("change");
                timespec ts[2];
                ts[0].tv_sec = last_access / 1000000000ULL;
                ts[0].tv_nsec = last_access % 1000000000Ull;
                ts[1].tv_sec = last_change / 1000000000ULL;
                ts[1].tv_nsec = last_change % 1000000000Ull;
                path->utimens(ts);
              }
              else if (op == "statfs")
              {
                require_path();
                struct statvfs sv;
                path->statfs(&sv);
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("path", pathname);
                response.serialize("f_bsize"  , uint64_t(sv.f_bsize)  );
                response.serialize("f_frsize" , uint64_t(sv.f_frsize) );
                response.serialize("f_blocks" , uint64_t(sv.f_blocks) );
                response.serialize("f_bfree"  , uint64_t(sv.f_bfree)  );
                response.serialize("f_bavail" , uint64_t(sv.f_bavail) );
                response.serialize("f_files"  , uint64_t(sv.f_files)  );
                response.serialize("f_ffree"  , uint64_t(sv.f_ffree)  );
                response.serialize("f_favail" , uint64_t(sv.f_favail) );
                response.serialize("f_fsid"   , uint64_t(sv.f_fsid)   );
                response.serialize("f_flag"   , uint64_t(sv.f_flag)   );
                response.serialize("f_namemax", uint64_t(sv.f_namemax));
                continue;
              }
              else if (op == "chown")
              {
                require_path();
                auto uid = command.deserialize<int>("uid");
                auto gid = command.deserialize<int>("gid");
                path->chown(uid, gid);
              }
              else if (op == "chmod")
              {
                require_path();
                auto mode = command.deserialize<int>("mode");
                path->chmod(mode);
              }
              else if (op == "write_file")
              {
                require_path();
                auto content = command.deserialize<std::string>("content");
                auto handle = path->create(O_TRUNC|O_CREAT, 0100666);
                handle->write(elle::WeakBuffer(elle::unconst(content.data()),
                                               content.size()),
                              content.size(), 0);
                handle->close();
              }
              else if (op == "read_file")
              {
                require_path();
                struct stat st;
                path->stat(&st);
                auto handle = path->open(O_RDONLY, 0666);
                auto content = std::string(st.st_size, char(0));
                handle->read(elle::WeakBuffer(elle::unconst(content.data()),
                                              content.size()),
                             st.st_size, 0);
                handle->close();
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("content", content);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("path", pathname);
                continue;
              }
              else if (op == "unlink")
              {
                require_path();
                path->unlink();
              }
              else if (op == "rmdir")
              {
                require_path();
                path->rmdir();
              }
              else if (op == "create")
              {
                require_path();
                int flags = command.deserialize<int>("flags");
                int mode = command.deserialize<int>("mode");
                auto handle = path->create(flags, mode);
                handles[handlename] = std::move(handle);
                handlename = "";
              }
              else if (op == "open")
              {
                require_path();
                int flags = command.deserialize<int>("flags");
                int mode = command.deserialize<int>("mode");
                auto handle = path->open(flags, mode);
                handles[handlename] = std::move(handle);
                handlename = "";
              }
              else if (op == "close")
              {
                handles.at(handlename)->close();
              }
              else if (op == "dispose")
              {
                handles.erase(handlename);
              }
              else if (op == "read")
              {
                auto offset = command.deserialize<uint64_t>("offset");
                auto size = command.deserialize<uint64_t>("size");
                elle::Buffer buf;
                buf.size(size);
                int nread = handles.at(handlename)->read(
                  elle::WeakBuffer(buf),
                  size, offset);
                buf.size(nread);
                auto response = elle::serialization::json::SerializerOut(std::cout);
                response.serialize("content", buf);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("handle", handlename);
                continue;
              }
              else if (op == "write")
              {
                auto offset = command.deserialize<uint64_t>("offset");
                auto size = command.deserialize<uint64_t>("size");
                elle::Buffer content = command.deserialize<elle::Buffer>("content");
                handles.at(handlename)->write(elle::WeakBuffer(content), size, offset);
              }
              else if (op == "ftruncate")
              {
                auto size = command.deserialize<uint64_t>("size");
                handles.at(handlename)->ftruncate(size);
              }
              else if (op == "fsync")
              {
                auto datasync = command.deserialize<int>("datasync");
                handles.at(handlename)->fsync(datasync);
              }
              else if (op == "fsyncdir")
              {
                auto datasync = command.deserialize<int>("datasync");
                handles.at(handlename)->fsyncdir(datasync);
              }
              else
                elle::err("operation %s does not exist", op);
              auto response = elle::serialization::json::SerializerOut(std::cout);
              response.serialize("success", true);
              response.serialize("operation", op);
              if (!handlename.empty())
                response.serialize("handle", handlename);
              if (!pathname.empty())
                response.serialize("path", pathname);
            }
            catch (elle::reactor::filesystem::Error const& e)
            {
              auto response = elle::serialization::json::SerializerOut(std::cout);
              response.serialize("success", false);
              response.serialize("message", e.what());
              response.serialize("code", e.error_code());
              response.serialize("operation", op);
              if (!handlename.empty())
                response.serialize("handle", handlename);
              if (!pathname.empty())
                response.serialize("path", pathname);
            }
            catch (elle::Error const& e)
            {
              if (input->eof())
                return;
              ELLE_LOG("bronk on op %s: %s", op, e);
              auto response = elle::serialization::json::SerializerOut(std::cout);
              response.serialize("success", false);
              response.serialize("message", e.what());
              response.serialize("operation", op);
              if (!handlename.empty())
                response.serialize("handle", handlename);
              if (!pathname.empty())
                response.serialize("path", pathname);
            }
          }
        }
        else
        {
          ELLE_TRACE("wait filesystem");
          elle::reactor::wait(*fs);
        }
      };
      if (local_endpoint && push_p)
        elle::With<InterfacePublisher>(
          ifnt, network, owner, model->id(), local_endpoint->port(),
          advertise_host,
          no_local_endpoints,
          no_public_endpoints) << [&]
        {
          run();
        };
      else
        run();
    }

    /*--------------.
    | Mode: start.  |
    `--------------*/

#ifndef INFINIT_WINDOWS
    void
    Volume::mode_start(std::string const& volume_name,
                       bool allow_root_creation,
                       boost::optional<std::string> mountpoint,
                       Defaulted<bool> readonly,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                       boost::optional<std::string> mount_name,
#endif
#ifdef INFINIT_MACOSX
                       boost::optional<std::string> mount_icon,
                       bool finder_sidebar,
#endif
                       Defaulted<bool> async,
#ifndef INFINIT_WINDOWS
                       bool daemon,
#endif
                       Defaulted<bool> monitoring,
                       Defaulted<Strings> fuse_option,
                       Defaulted<bool> cache,
                       boost::optional<int> cache_ram_size,
                       boost::optional<int> cache_ram_ttl,
                       boost::optional<int> cache_ram_invalidation,
                       boost::optional<uint64_t> cache_disk_size,
                       Defaulted<bool> fetch_endpoints,
                       Defaulted<bool> fetch,
                       Defaulted<Strings> peer,
                       Defaulted<bool> push_endpoints,
                       bool register_service,
                       bool no_local_endpoints,
                       bool no_public_endpoints,
                       Defaulted<bool> push,
                       bool map_other_permissions,
                       Defaulted<bool> publish,
                       Strings advertise_host,
                       boost::optional<std::string> endpoints_file,
                       boost::optional<std::string> port_file,
                       boost::optional<int> port,
                       boost::optional<std::string> listen,
                       Defaulted<int> fetch_endpoints_interval,
                       boost::optional<std::string> input)
    {
      ELLE_TRACE_SCOPE("start");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(volume_name, owner);

      auto mo = infinit::MountOptions{};
      MOUNT_OPTIONS_MERGE(mo);
      elle::reactor::network::UnixDomainSocket sock(daemon_sock_path());
      auto cmd = [&]
        {
          std::stringstream ss;
          auto cmd = elle::serialization::json::SerializerOut(ss, false);
          cmd.serialize("operation", "volume-start");
          cmd.serialize("volume", name);
          cmd.serialize("options", mo);
          return ss.str();
        }();
      sock.write(elle::ConstWeakBuffer(cmd));
      auto reply = sock.read_until("\n").string();
      std::stringstream replystream(reply);
      auto json = elle::json::read(replystream);
      auto jsono = boost::any_cast<elle::json::Object>(json);
      if (boost::any_cast<std::string>(jsono.at("result")) != "Ok")
      {
        std::cout << elle::json::pretty_print(json) << std::endl;
        throw elle::Exit(1);
      }
      else
        std::cout << "Ok" << std::endl;
    }
#endif

    /*---------------.
    | Mode: status.  |
    `---------------*/

#ifndef INFINIT_WINDOWS
    void
    Volume::mode_status(std::string const& volume_name)
    {
      ELLE_TRACE_SCOPE("status");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(volume_name, owner);

      elle::reactor::network::UnixDomainSocket sock(daemon_sock_path());
      auto cmd = [&]
        {
          std::stringstream ss;
          auto cmd = elle::serialization::json::SerializerOut(ss, false);
          cmd.serialize("operation", "volume-status");
          cmd.serialize("volume", name);
          return ss.str();
        }();
      sock.write(elle::ConstWeakBuffer(cmd));
      auto reply = sock.read_until("\n").string();
      std::stringstream replystream(reply);
      auto json = elle::json::read(replystream);
      auto jsono = boost::any_cast<elle::json::Object>(json);
      if (boost::any_cast<std::string>(jsono.at("result")) != "Ok"
        || !boost::any_cast<bool>(jsono.at("live")))
      {
        std::cout << elle::json::pretty_print(json) << std::endl;
        throw elle::Exit(1);
      }
      else
        std::cout << "Ok" << std::endl;
    }
#endif

    /*-------------.
    | Mode: stop.  |
    `-------------*/

#ifndef INFINIT_WINDOWS
    void
    Volume::mode_stop(std::string const& volume_name)
    {
      ELLE_TRACE_SCOPE("stop");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto name = ifnt.qualified_name(volume_name, owner);
      elle::reactor::network::UnixDomainSocket sock(daemon_sock_path());
      auto cmd = [&]
        {
          std::stringstream ss;
          auto cmd = elle::serialization::json::SerializerOut(ss, false);
          cmd.serialize("operation", "volume-stop");
          cmd.serialize("volume", name);
          return ss.str();
        }();
      sock.write(elle::ConstWeakBuffer(cmd));
      auto reply = sock.read_until("\n").string();
      std::stringstream replystream(reply);
      auto json = elle::json::read(replystream);
      auto jsono = boost::any_cast<elle::json::Object>(json);
      if (boost::any_cast<std::string>(jsono.at("result")) != "Ok")
      {
        std::cout << elle::json::pretty_print(json) << std::endl;
        throw elle::Exit(1);
      }
      else
        std::cout << "Ok" << std::endl;
    }
#endif


    /*---------------.
    | Mode: update.  |
    `---------------*/

    void
    Volume::mode_update(std::string const& volume_name,
                        boost::optional<std::string> description,
                        bool allow_root_creation,
                        boost::optional<std::string> mountpoint,
                        Defaulted<bool> readonly,
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                        boost::optional<std::string> mount_name,
#endif
#ifdef INFINIT_MACOSX
                        boost::optional<std::string> mount_icon,
                        bool finder_sidebar,
#endif
                        Defaulted<bool> async,
#ifndef INFINIT_WINDOWS
                        bool daemon,
#endif
                        Defaulted<bool> monitoring,
                        Defaulted<Strings> fuse_option,
                        Defaulted<bool> cache,
                        boost::optional<int> cache_ram_size,
                        boost::optional<int> cache_ram_ttl,
                        boost::optional<int> cache_ram_invalidation,
                        boost::optional<uint64_t> cache_disk_size,
                        Defaulted<bool> fetch_endpoints,
                        Defaulted<bool> fetch,
                        Defaulted<Strings> peer,
                        Defaulted<bool> push_endpoints,
                        Defaulted<bool> push,
                        bool map_other_permissions,
                        Defaulted<bool> publish,
                        Strings advertise_host,
                        boost::optional<std::string> endpoints_file,
                        boost::optional<std::string> port_file,
                        boost::optional<int> port,
                        boost::optional<std::string> listen,
                        Defaulted<int> fetch_endpoints_interval,
                        boost::optional<std::string> input,
                        boost::optional<std::string> user,
                        boost::optional<int> block_size)
    {
      ELLE_TRACE_SCOPE("update");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      // In update mode, push is an alias for push_endpoint, not
      // push_volume.  So keep push_volume's original value to decide
      // whether to push the volume.
      auto push_volume = *push;
      // Normalize options *before* merging them into MountOptions.
      resolve_aliases(fetch, fetch_endpoints);
      resolve_aliases(push, push_endpoints);

      auto name = ifnt.qualified_name(volume_name, owner);
      auto volume = ifnt.volume_get(name);

      MOUNT_OPTIONS_MERGE(volume.mount_options);
      if (description)
        volume.description = description;
      if (block_size)
        volume.block_size = block_size;
      ifnt.volume_save(volume, true);
      if (push_volume)
        ifnt.beyond_push("volume", name, volume, owner);
    }

    Volume::Syscall::Syscall(Infinit& infinit)
      : Object(infinit)
      , getxattr(*this,
                 "Get an extended attribute value",
                 cli::path,
                 cli::name)
      , setxattr(*this,
                 "Set an extended attribute value",
                 cli::path,
                 cli::name,
                 cli::value)
    {}

    /*----------------.
    | Mode: get_xattr |
    `----------------*/
    void
    Volume::Syscall::mode_get_xattr(std::string const& path,
                                    std::string const& name)
    {
      char result[16384];
      int length = get_xattr(path, name, result, sizeof(result) - 1, true);
      result[length] = 0;
      std::cout << result << std::endl;
    }

    /*----------------.
    | Mode: set_xattr |
    `----------------*/
    void
    Volume::Syscall::mode_set_xattr(std::string const& path,
                                    std::string const& name,
                                    std::string const& value)
    {
      set_xattr(path, name, value, true);
    }
  }
}
