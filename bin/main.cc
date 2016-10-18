#include <elle/log.hh>
#include <elle/string/algorithm.hh>
#include <elle/system/unistd.hh>

#include <reactor/FDStream.hh>

ELLE_LOG_COMPONENT("infinit");

#ifndef INFINIT_WINDOWS
# include <crash_reporting/CrashReporter.hh>
#endif

#include <main.hh>

reactor::Thread::unique_ptr
make_stat_update_thread(infinit::User const& self,
                        infinit::Network& network,
                        infinit::model::doughnut::Doughnut& model)
{
  auto notify = [&]
    {
      network.notify_storage(self, model.id());
    };
  model.local()->storage()->register_notifier(notify);
  return reactor::every(60_min, "periodic storage stat updater", notify);
}

infinit::model::doughnut::Protocol
protocol_get(boost::program_options::variables_map const& args)
{
  std::string proto = args["protocol"].as<std::string>();
  try
  {
    return elle::serialization::Serialize<
      infinit::model::doughnut::Protocol>::convert(proto);
  }
  catch (elle::serialization::Error const& e)
  {
    throw CommandLineError("'protocol' must be 'utp', 'tcp' or 'all'");
  }
}

bool
is_version_supported(elle::Version const& version)
{
  auto const& deps = infinit::serialization_tag::dependencies;
  return std::find_if(deps.begin(), deps.end(),
                      [version] (auto const& kv) -> bool
                      {
                        return kv.first.major() == version.major() &&
                          kv.first.minor() == version.minor();
                      }) != deps.end();
}

void
ensure_version_is_supported(elle::Version const& version)
{
  if (!is_version_supported(version))
  {
    auto const& deps = infinit::serialization_tag::dependencies;
    std::vector<elle::Version> supported_versions(deps.size());
    std::transform(
      deps.begin(), deps.end(), supported_versions.begin(),
      [] (auto const& kv)
      {
        return elle::Version{kv.first.major(), kv.first.minor(), 0};
      });
    std::sort(supported_versions.begin(), supported_versions.end());
    supported_versions.erase(
      std::unique(supported_versions.begin(), supported_versions.end()),
      supported_versions.end());
    // Find the max value for the major.
    std::vector<elle::Version> versions_for_major;
    std::copy_if(supported_versions.begin(), supported_versions.end(),
                 std::back_inserter(versions_for_major),
                 [&] (elle::Version const& c)
                 {
                   return c.major() == version.major();
                 });
    if (versions_for_major.size() > 0)
    {
      if (version < versions_for_major.front())
        elle::err("Minimum compatibility version for major version %s is %s",
                  (int) version.major(), supported_versions.front());
      else if (version > versions_for_major.back())
        elle::err("Maximum compatibility version for major version %s is %s",
                  (int) version.major(), versions_for_major.back());
    }
    elle::err("Unknown compatibility version, try one of %s",
              elle::join(supported_versions.begin(),
                         supported_versions.end(),
                         ", "));
  }
}

reactor::Thread::unique_ptr
make_poll_beyond_thread(infinit::model::doughnut::Doughnut& model,
                        infinit::Network& network,
                        infinit::overlay::NodeLocations const& locs_,
                        int interval)
{
  auto poll = [&, locs_, interval]
    {
      infinit::overlay::NodeLocations locs = locs_;
      while (true)
      {
        reactor::sleep(boost::posix_time::seconds(interval));
        infinit::overlay::NodeLocations news;
        try
        {
          beyond_fetch_endpoints(network, news, true);
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("exception fetching endpoints: %s", e);
          continue;
        }
        std::unordered_set<infinit::model::Address> new_addresses;
        for (auto const& n: news)
        {
          new_addresses.insert(n.id());
          auto uid = n.id();
          auto it = std::find_if(locs.begin(), locs.end(),
            [&](infinit::model::NodeLocation const& nl) { return nl.id() == uid;});
          if (it == locs.end())
          {
            ELLE_TRACE("calling discover() on new peer %s", n);
            locs.emplace_back(n);
            model.overlay()->discover({n});
          }
          else if (it->endpoints() != n.endpoints())
          {
            ELLE_TRACE("calling discover() on updated peer %s", n);
            it->endpoints() = n.endpoints();
            model.overlay()->discover({n});
          }
        }
        auto it = std::remove_if(locs.begin(), locs.end(),
          [&](infinit::model::NodeLocation const& nl)
          {
            return new_addresses.find(nl.id()) == new_addresses.end();
          });
        locs.erase(it, locs.end());
      }
    };
  return reactor::Thread::unique_ptr(new reactor::Thread("beyond poller", poll));
}

// Return arguments for mode in the correct order.
// Note: just using "collect_unrecognized" is insufficient as this changes
// the order and does not handle the case where the binary has a mode and
// mode argument with the same name.
static
std::vector<std::string>
mode_arguments(
  std::vector<std::string> tokens,
  Mode const* mode,
  Modes const& modes, boost::optional<Modes> hidden_modes,
  boost::program_options::basic_parsed_options<char> const& parsed)
{
  bool after_mode = false;
  std::vector<std::string> res;
  // Valid tokens are modes + unrecognized options.
  std::vector<std::string> valid_tokens;
  bool handling_valid_option = false;
  for (auto const& m: modes)
    valid_tokens.push_back(elle::sprintf("--%s", m.name));
  if (hidden_modes)
  {
    for (auto const& m: hidden_modes.get())
      valid_tokens.push_back(elle::sprintf("--%s", m.name));
  }
  for (auto val: boost::program_options::collect_unrecognized(
        parsed.options, boost::program_options::include_positional))
    valid_tokens.push_back(val);
  for (auto const& token: tokens)
  {
    if (token == elle::sprintf("--%s", mode->name) && !after_mode)
    {
      after_mode = true;
      // Handle case where token directly after mode is positional argument.
      // e.g.: infinit-user --create bob
      handling_valid_option = true;
      continue;
    }
    if (!after_mode)
      continue;
    // We're after the mode in the tokens, check if we have a valid token.
    if (std::find(valid_tokens.begin(), valid_tokens.end(), token)
        != valid_tokens.end())
    {
      // Check if token is an option descriptor.
      // Could start with - or --
      if (token.find("-") == 0)
        handling_valid_option = true;
      // Only push back valid option descriptors and their associated values.
      if (handling_valid_option)
        res.push_back(token);
    }
    // If we were passed an option descriptor, it's not a valid one. Ignore
    // proceeding value.
    else if (token.find("-") == 0)
      handling_valid_option = false;
    // Assume invalid option descriptors are not multitoken and accept next
    // token as it is a positional argument.
    // e.g.: infinit-network --export --as owner network
    else
      handling_valid_option = true;
  }
  return res;
}

namespace infinit
{
  std::unique_ptr<std::istream>
  commands_input(boost::program_options::variables_map const& args)
  {
    if (args.count("input"))
    {
      auto path = args["input"].as<std::string>();
      if (path != "-")
      {
        auto file = elle::make_unique<boost::filesystem::ifstream>(path);
        if (!file->good())
          elle::err("unable to open \"%s\" for reading", path);
        return std::move(file);
      }
    }
#ifndef INFINIT_WINDOWS
    return elle::make_unique<reactor::FDStream>(0);
#else
    // Windows does not support async io on stdin
    auto res = elle::make_unique<std::stringstream>();
    while (true)
    {
      char buf[4096];
      std::cin.read(buf, 4096);
      if (int count = std::cin.gcount())
        res->write(buf, count);
      else
        break;
    }
    return res;
#endif
  }

  boost::optional<elle::Version> compatibility_version;
  int
  main(std::string desc,
       Modes const& modes,
       int argc,
       char** argv,
       boost::optional<std::string> positional_arg,
       boost::optional<bool> disable_as_arg,
       boost::optional<Modes> hidden_modes)
  {
    try
    {
      program = argv[0];
      std::string crash_host(elle::os::getenv("INFINIT_CRASH_REPORT_HOST", ""));
#if  !defined(INFINIT_WINDOWS) && !defined(NO_EXECINFO)
      std::unique_ptr<crash_reporting::CrashReporter> crash_reporter;
#ifdef INFINIT_PRODUCTION_BUILD
      bool const production_build = true;
#else
      bool const production_build = false;
#endif
      if (production_build &&
          elle::os::getenv("INFINIT_CRASH_REPORTER", "") != "0" ||
          elle::os::getenv("INFINIT_CRASH_REPORTER", "") == "1")
      {
        std::string crash_url =
          elle::sprintf("%s/crash/report",
                        crash_host.length() ? crash_host : beyond());
        auto dumps_path = canonical_folder(xdg_cache_home() / "crashes");
        crash_reporter = elle::make_unique<crash_reporting::CrashReporter>(
          crash_url, dumps_path, version_describe);
      }
#endif
      reactor::Scheduler sched;
      reactor::Thread main_thread(
        sched,
        "main",
        [&sched, &modes, &desc, argc, argv,
         &positional_arg, &disable_as_arg, &hidden_modes, &main_thread
#if !defined(INFINIT_WINDOWS) && !defined(NO_EXECINFO)
         , &crash_reporter
#endif
          ]
        {
#ifdef INFINIT_LINUX
          // boost::filesystem uses the default locale, detect here if it
          // cant be instantiated.
          // Not required on OS X, see: boost/libs/filesystem/src/path.cpp:819
          check_broken_locale();
#endif
          ELLE_TRACE("parse command line")
          {
            using boost::program_options::value;
            Mode::OptionsDescription mode_options("Modes");
            for (auto const& mode: modes)
              mode_options.add_options()(mode.name, mode.description);
            Mode::OptionsDescription hidden_mode_options("Hidden Modes");
            if (hidden_modes)
            {
              for (auto const& mode: hidden_modes.get())
                hidden_mode_options.add_options()(mode.name, mode.description);
            }
            Mode::OptionsDescription options(desc);
            Mode::OptionsDescription visible_options(desc);
            auto add_options =
              [&] (boost::program_options::options_description const& m_options,
                   bool visible_in_production = true)
              {
                if (m_options.options().size() == 0)
                  return;
                options.add(m_options);
                if (visible_in_production || show_hidden_options())
                  visible_options.add(m_options);
              };
            add_options(mode_options);
            add_options(hidden_mode_options, false);
            Mode::OptionsDescription misc("Miscellaneous");
            misc.add_options()
              ("help,h", "display the help")
              ("script,s", "silence all extraneous human friendly messages")
              ("compatibility-version", value<std::string>(),
               "force compatibility version")
              ("version,v", "display version")
              ("critical-log", "Path to critical log file, none to disable")
              ;
            if (!disable_as_arg || !disable_as_arg.get())
            {
              misc.add_options()
                ("as,a", value<std::string>(),
                 "user to run commands as (default: system user)")
                ;
            }
            add_options(misc);
            boost::program_options::variables_map vm;
            try
            {
              namespace po = boost::program_options;
              auto parser = po::command_line_parser(argc, argv);
              auto style =
                static_cast<int>(po::command_line_style::default_style);
              style &= ~po::command_line_style::allow_guessing;
              parser.style(style);
              parser.options(options);
              parser.allow_unregistered();
              auto parsed = parser.run();
              store(parsed, vm);
              notify(vm);
              if (vm.count("compatibility-version"))
              {
                compatibility_version = elle::Version::from_string(
                  vm["compatibility-version"].as<std::string>());
                ensure_version_is_supported(*compatibility_version);
              }
              if (vm.count("version"))
              {
                std::cout << version_describe << std::endl;
                throw elle::Exit(0);
              }
              auto critical_log_file = xdg_state_home() / "critical.log";
              bool critical_log_disabled = false;
              if (vm.count("critical-log"))
              {
                critical_log_file = vm["critical-log"].as<std::string>();
                critical_log_disabled =
                  (vm["critical-log"].as<std::string>() == "none");
              }
              if (!critical_log_disabled)
              {
                ELLE_TRACE("Initializing critical logger at %s",
                           critical_log_file);
                boost::filesystem::create_directories(
                  critical_log_file.parent_path());
                std::unique_ptr<elle::log::CompositeLogger> cl
                  = elle::make_unique<elle::log::CompositeLogger>();
                auto clptr = cl.get();
                auto prev_logger = elle::log::logger(std::move(cl));
                critical_log_stream.reset(new std::ofstream(
                                            critical_log_file.string(),
                                            std::fstream::app | std::fstream::out));
                boost::optional<std::string> env_log_level;
                if (elle::os::inenv("ELLE_LOG_LEVEL"))
                  env_log_level = elle::os::getenv("ELLE_LOG_LEVEL");
                elle::os::unsetenv("ELLE_LOG_LEVEL");
                std::unique_ptr<elle::log::Logger> crit
                  = elle::make_unique<elle::log::TextLogger>(
                    *critical_log_stream, "LOG", false, true,
                    false, true, false, true, true);
                if (env_log_level)
                  elle::os::setenv("ELLE_LOG_LEVEL", *env_log_level, true);
                clptr->loggers().push_back(std::move(crit));
                // log to crit only goes here
                clptr->loggers().push_back(std::move(prev_logger));
              }
              bool help = vm.count("help");
              script_mode = vm.count("script");
              if (vm.count("as"))
                _as_user = vm["as"].as<std::string>();
              Mode const* mode = nullptr;
              bool misplaced_mode = false;
              std::vector<std::string> tokens;
              for (int i = 0; i < argc; i++)
                tokens.emplace_back(std::string(argv[i]));
              auto get_mode = [&] (Modes const& modes)
                {
                  int pos = std::distance(tokens.begin(), tokens.end());
                  for (auto const& m: modes)
                  {
                    if (vm.count(m.name))
                    {
                      int new_pos = std::distance(
                        tokens.begin(),
                        std::find(tokens.begin(), tokens.end(),
                                  elle::sprintf("--%s", m.name)));
                      if (new_pos < pos)
                      {
                        mode = &m;
                        pos = new_pos;
                      }
                    }
                  }
                  if (mode && pos != 1)
                    misplaced_mode = true;
                };
              get_mode(modes);
              if (mode == nullptr && hidden_modes)
                get_mode(hidden_modes.get());
              if (misplaced_mode)
              {
                (help ? std::cout : std::cerr) << "MODE must be the first argument." << std::endl;
                mode = nullptr;
              }
              if (!mode)
              {
                std::ostream& output = help ? std::cout : std::cerr;
                output << "Usage: " << program
                       << " MODE [OPTIONS...]" << std::endl;
                output << std::endl;
                output << visible_options;
                output << std::endl;
                if (help)
                  throw elle::Exit(0);
                else
                  throw elle::Error("mode unspecified");
              }
              if (help)
              {
                mode->help(std::cout);
                return;
              }
              std::unique_ptr<reactor::Thread> crash_upload_thread;
#if !defined(INFINIT_WINDOWS) && !defined(NO_EXECINFO)
              if (crash_reporter && crash_reporter->crashes_pending_upload())
              {
                crash_upload_thread.reset(new reactor::Thread("upload crashes",
                                                              [&crash_reporter]
                                                              {
                                                                crash_reporter->upload_existing();
                                                              }));
              }
#endif
              boost::signals2::signal<void ()> killed;
              if (!getenv("INFINIT_DISABLE_SIGNAL_HANDLER"))
              {
                static const std::vector<int> signals = {SIGINT, SIGTERM
#ifndef INFINIT_WINDOWS
                                                         , SIGQUIT
#endif
                };
                for (auto signal: signals)
                {
#ifndef INFINIT_WINDOWS
                  ELLE_DEBUG("set signal handler for %s", strsignal(signal));
#endif
                  reactor::scheduler().signal_handle(
                    signal,
                    [&]
                    {
                      main_thread.terminate();
                      killed();
                    });
                }
              }
              try
              {
                std::vector<std::string> args =
                  mode_arguments(tokens, mode, modes, hidden_modes, parsed);
                mode->action(
                  parse_args(mode->options, args,
                             (positional_arg ? positional_arg.get() : "name")),
                  killed);
                if (crash_upload_thread)
                  reactor::wait(*crash_upload_thread);
              }
              catch (CommandLineError const&)
              {
                mode->help(std::cerr);
                if (crash_upload_thread)
                  reactor::wait(*crash_upload_thread);
                throw;
              }
              catch (boost::program_options::error_with_option_name const& e)
              {
                mode->help(std::cerr);
                if (crash_upload_thread)
                  reactor::wait(*crash_upload_thread);
                throw;
              }
              catch (elle::Exception const&)
              {
                if (crash_upload_thread)
                  reactor::wait(*crash_upload_thread);
                throw;
              }
            }
            catch (boost::program_options::error_with_option_name const& e)
            {
              throw elle::Error(
                elle::sprintf("command line error: %s", e.what()));
            }
          }
        });
      sched.run();
    }
    catch (elle::Exit const& e)
    {
      return e.return_code();
    }
    catch (elle::Exception const& e)
    {
      ELLE_TRACE("fatal error: %s\n%s", e.what(), e.backtrace());
      if (!elle::os::getenv("INFINIT_BACKTRACE", "").empty())
      {
        elle::fprintf(std::cerr, "%s: fatal error: %s\n%s\n", argv[0],
                      e.what(), e.backtrace());
      }
      else
      {
        elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
      }
      return 1;
    }
    catch (std::exception const& e)
    {
      ELLE_TRACE("fatal error: %s", e.what());
      elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
      return 1;
    }
    return 0;
  }

#ifndef INFINIT_WINDOWS
  DaemonHandle
  daemon_hold(int nochdir, int noclose)
  {
    int pipefd[2]; // reader, writer
    if (pipe(pipefd))
      throw elle::Error(strerror(errno));
    int cpid = fork();
    if (cpid == -1)
      throw elle::Error(strerror(errno));
    else if (cpid == 0)
    { // child
      if (setsid()==-1)
        throw elle::Error(strerror(errno));
      if (!nochdir)
        elle::chdir("/");
      if (!noclose)
      {
        int fd = open("/dev/null", O_RDWR);
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
      }
      close(pipefd[0]);
      return pipefd[1];
    }
    else
    { // parent
      close(pipefd[1]);
      char buf;
      int res = read(pipefd[0], &buf, 1);
      ELLE_LOG("DETACHING %s %s", res, strerror(errno));
      if (res < 1)
        exit(1);
      else
        exit(0);
    }
  }
  void
  daemon_release(DaemonHandle handle)
  {
    char buf = 1;
    if (write(handle, &buf, 1)!=1)
      perror("daemon_release");
  }
#endif

  model::NodeLocations
  hook_peer_discovery(model::doughnut::Doughnut& model, std::string file)
  {
    ELLE_TRACE("Hooking discovery on %s, to %s", model, file);
    auto nls = std::make_shared<model::NodeLocations>();
    model.overlay()->on_discover().connect(
      [nls, file] (model::NodeLocation nl, bool observer) {
        if (observer)
          return;
        auto it = std::find_if(nls->begin(), nls->end(),
          [id=nl.id()] (model::NodeLocation n) {
            return n.id() == id;
          });
        if (it == nls->end())
          nls->push_back(nl);
        else
          it->endpoints() = nl.endpoints();
        ELLE_DEBUG("Storing updated endpoint list: %s", *nls);
        std::ofstream ofs(file);
        elle::serialization::json::serialize(*nls, ofs, false);
      });
    model.overlay()->on_disappear().connect(
      [nls, file] (model::Address id, bool observer) {
        if (observer)
          return;
        auto it = std::find_if(nls->begin(), nls->end(),
          [id] (model::NodeLocation n) {
            return n.id() == id;
          });
        if (it != nls->end())
          nls->erase(it);
        ELLE_DEBUG("Storing updated endpoint list: %s", *nls);
        std::ofstream ofs(file);
        elle::serialization::json::serialize(*nls, ofs, false);
      });
    if (boost::filesystem::exists(file) && !boost::filesystem::is_empty(file))
    {
      ELLE_DEBUG("Reloading endpoint list file from %s", file);
      std::ifstream ifs(file);
      return elle::serialization::json::deserialize<model::NodeLocations>(ifs, false);
    }
    return model::NodeLocations();
  }
}

std::string program;
bool script_mode = false;
boost::optional<std::string> _as_user = {};
