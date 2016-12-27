#include <infinit/cli/Volume.hh>

#include <infinit/MountOptions.hh>
#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>
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
    namespace bfs = boost::filesystem;

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
                   cli::input = boost::none))
      , export_(
        "Export a volume for someone else to import",
        das::cli::Options(),
        this->bind(modes::mode_export,
                   cli::name,
                   cli::output = boost::none))
      , fetch(
        "Fetch a volume from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_fetch,
                   cli::name = boost::none,
                   cli::network = boost::none,
                   cli::service = false))
      , import(
        "Import a volume",
        das::cli::Options(),
        this->bind(modes::mode_import,
                   cli::input = boost::none,
                   cli::mountpoint = boost::none))
      , pull(
        "Remove a volume from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_pull,
                   cli::name,
                   cli::purge = false))
      , push(
        "Push a volume to {hub}",
        das::cli::Options(),
        this->bind(modes::mode_push,
                   cli::name))
      , run(
        "Run a volume",
        das::cli::Options(),
        this->bind(modes::mode_run,
                   cli::name,
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
                   cli::register_service = false,
                   cli::no_local_endpoints = false,
                   cli::no_public_endpoints = false,
                   cli::push = false,
                   cli::map_other_permissions = true,
                   cli::publish = false,
                   cli::advertise_host = Strings{},
                   cli::endpoints_file = boost::none,
                   cli::port_file = boost::none,
                   cli::port = boost::none,
                   cli::listen = boost::none,
                   cli::fetch_endpoints_interval = 300,
                   cli::input = boost::none,
                   cli::disable_UTF_8_conversion = false))
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
          elle::err<Error>("--network is mandatory with --service");
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
              ifnt.volume_save(v);
              cli.report_saved("volume", v.name);
            }
      }
      else if (volume_name)
      {
        auto name = ifnt.qualified_name(*volume_name, owner);
        auto desc = ifnt.beyond_fetch<infinit::Volume>("volume", name);
        ifnt.volume_save(std::move(desc));
      }
      else if (network_name)
      {
        // Fetch all networks for network.
        auto net_name = ifnt.qualified_name(*network_name, owner);
        auto res = ifnt.beyond_fetch<VolumesMap>(
            elle::sprintf("networks/%s/volumes", net_name),
            "volumes for network",
            net_name);
        for (auto const& volume: res["volumes"])
          ifnt.volume_save(std::move(volume));
      }
      else
      {
        // Fetch all networks for owner.
        auto res = ifnt.beyond_fetch<VolumesMap>(
            elle::sprintf("users/%s/volumes", owner.name),
            "volumes for user",
            owner.name,
            owner);
        for (auto const& volume: res["volumes"])
          try
          {
            ifnt.volume_save(std::move(volume));
          }
          catch (infinit::ResourceAlreadyFetched const& error)
          {
          }
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
      auto name = ifnt.qualified_name(volume_name, owner);
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
      auto name = ifnt.qualified_name(volume_name, owner);
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

#ifdef INFINIT_MACOSX
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
          LSSharedFileListItemRef item_ref =
            (LSSharedFileListItemRef)CFArrayGetValueAtIndex(items_array, i);
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
#endif

    void
    Volume::mode_run(std::string const& volume_name,
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
                     bool register_service,
                     bool no_local_endpoints,
                     bool no_public_endpoints,
                     bool push,
                     bool map_other_permissions,
                     bool publish,
                     Strings advertise_host,
                     boost::optional<std::string> endpoints_file,
                     boost::optional<std::string> port_file,
                     boost::optional<int> port,
                     boost::optional<std::string> listen,
                     int fetch_endpoints_interval,
                     boost::optional<std::string> input_name,
                     bool disable_UTF_8_conversion)
    {
      ELLE_TRACE_SCOPE("run");
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();

      // Normalize options *before* merging them into MountOptions.
      fetch |= fetch_endpoints;
      push |= push_endpoints;

      auto name = ifnt.qualified_name(volume_name, owner);
      auto volume = ifnt.volume_get(name);
      auto& mo = volume.mount_options;
      MOUNT_OPTIONS_MERGE(mo);
#ifdef INFINIT_MACOSX
      if (mo.mountpoint && !disable_UTF_8_conversion)
        mo.fuse_options.emplace_back("modules=iconv,from_code=UTF-8,to_code=UTF-8-MAC");
#endif
      bool created_mountpoint = false;
      if (mo.mountpoint)
      {
#ifdef INFINIT_WINDOWS
        if (mo.mountpoint.get().size() == 2 && mo.mountpoint.get()[1] == ':')
          ;
        else
#elif defined(INFINIT_MACOSX)
        // Do not try to create folder in /Volumes.
        auto mount_path = bfs::path(mo.mountpoint.get());
        auto mount_parent = mount_path.parent_path().string();
        boost::algorithm::to_lower(mount_parent);
        if (mount_parent.find("/volumes") == 0)
          ;
        else
#endif
        try
        {
          if (bfs::exists(mo.mountpoint.get()))
          {
            if (!bfs::is_directory(mo.mountpoint.get()))
              elle::err("mountpoint is not a directory");
            if (!bfs::is_empty(mo.mountpoint.get()))
              elle::err("mountpoint is not empty");
          }
          created_mountpoint =
            bfs::create_directories(mo.mountpoint.get());
        }
        catch (bfs::filesystem_error const& e)
        {
          elle::err("unable to access mountpoint: %s", e.what());
        }
      }
      if (!mo.fuse_options.empty() && !mo.mountpoint)
        elle::err<Error>("FUSE options require the volume to be mounted");
      auto network = ifnt.network_get(volume.network, owner);
      network.ensure_allowed(owner, "run", "volume");
      ELLE_TRACE("run network");
#ifndef INFINIT_WINDOWS
      auto daemon_handle = daemon ? daemon_hold(0, 1) : daemon_invalid;
#endif
      cli.report_action("running", "network", network.name);
      auto model_and_threads = network.run(
        owner, mo, true, monitoring,
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
      // Only push if we have are contributing storage.
      bool push_p = mo.push && model->local();
      auto local_endpoint = boost::optional<infinit::model::Endpoint>{};
      if (model->local())
      {
        local_endpoint = model->local()->server_endpoint();
        if (port_file)
          port_to_file(local_endpoint.get().port(), *port_file);
        if (endpoints_file)
          endpoints_to_file(model->local()->server_endpoints(),
                            *endpoints_file);
      }
      auto run = [&, push_p]
      {
        reactor::Thread::unique_ptr stat_thread;
        if (push_p)
          stat_thread = network.make_stat_update_thread(owner, *model);
        ELLE_TRACE_SCOPE("run volume");
        cli.report_action("running", "volume", volume.name);
        auto fs = volume.run(std::move(model),
                             mo.mountpoint,
                             mo.readonly,
                             allow_root_creation,
                             map_other_permissions
#if defined INFINIT_MACOSX || defined INFINIT_WINDOWS
                             , mount_name
#endif
#ifdef INFINIT_MACOSX
                             , mount_icon
#endif
                             );
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
#   if 0
        auto root_poller = boost::optional<std::thread>;
        if (mo.mountpoint && mo.cache && mo.cache.get())
          root_poller.emplace(
            [root = mo.mountpoint.get()]
            {
              try
              {
                bfs::status(root);
                for (auto it = bfs::directory_iterator(root);
                     it != bfs::directory_iterator();
                     ++it)
                  ;
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
#   endif
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
          reactor::background([mountpoint]
            {
              add_path_to_finder_sidebar(mountpoint.get());
            });
        }
        auto reachability = std::unique_ptr<reactor::network::Reachability>{};
#endif
        elle::SafeFinally unmount([&]
        {
#ifdef INFINIT_MACOSX
          if (reachability)
            reachability->stop();
          if (finder_sidebar && mo.mountpoint)
          {
            auto mountpoint = mo.mountpoint;
            reactor::background([mountpoint]
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
        if (elle::os::getenv("INFINIT_LOG_REACHABILITY", "") != "0")
        {
          reachability.reset(new reactor::network::Reachability(
            {},
            [&] (reactor::network::Reachability::NetworkStatus status)
            {
              using NetworkStatus = reactor::network::Reachability::NetworkStatus;
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
                               std::unique_ptr<reactor::filesystem::Handle>>{};
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
              elle::serialization::json::SerializerIn command(json, false);
              op = command.deserialize<std::string>("operation");
              std::shared_ptr<reactor::filesystem::Path> path;
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
                    throw reactor::filesystem::Error(
                      ENOENT,
                      elle::sprintf("no such file or directory: %s", pathname));
                };
              if (op == "list_directory")
              {
                require_path();
                std::vector<std::string> entries;
                path->list_directory(
                  [&] (std::string const& path, struct stat*)
                  {
                    entries.push_back(path);
                  });
                elle::serialization::json::SerializerOut response(std::cout);
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
                elle::serialization::json::SerializerOut response(std::cout);
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
                elle::serialization::json::SerializerOut response(std::cout);
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
                elle::serialization::json::SerializerOut response(std::cout);
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
                elle::serialization::json::SerializerOut response(std::cout);
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
                elle::serialization::json::SerializerOut response(std::cout);
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
                uint64_t offset = command.deserialize<uint64_t>("offset");
                uint64_t size = command.deserialize<uint64_t>("size");
                elle::Buffer buf;
                buf.size(size);
                int nread = handles.at(handlename)->read(
                  elle::WeakBuffer(buf),
                  size, offset);
                buf.size(nread);
                elle::serialization::json::SerializerOut response(std::cout);
                response.serialize("content", buf);
                response.serialize("success", true);
                response.serialize("operation", op);
                response.serialize("handle", handlename);
                continue;
              }
              else if (op == "write")
              {
                uint64_t offset = command.deserialize<uint64_t>("offset");
                uint64_t size = command.deserialize<uint64_t>("size");
                elle::Buffer content = command.deserialize<elle::Buffer>("content");
                handles.at(handlename)->write(elle::WeakBuffer(content), size, offset);
              }
              else if (op == "ftruncate")
              {
                uint64_t size = command.deserialize<uint64_t>("size");
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
            catch (reactor::filesystem::Error const& e)
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
          reactor::wait(*fs);
        }
      };
      if (local_endpoint && push_p)
        elle::With<InterfacePublisher>(
          network, owner, model->id(), local_endpoint.get().port(), advertise_host,
          no_local_endpoints,
          no_public_endpoints) << [&]
        {
          run();
        };
      else
        run();
    }
  }
}
