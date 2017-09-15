#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/algorithm/transform.hpp>

#include <elle/Exit.hh>
#include <elle/log.hh>

#include <memo/cli/Memo.hh>
#include <memo/cli/utility.hh>
#include <memo/crash-report.hh>
#include <memo/environ.hh>

ELLE_LOG_COMPONENT("memo.main");

namespace
{
  /// How the user called us.
  auto argv_0 = std::string("memo");
  /// The path to `memo`.
  auto memo_exe = argv_0;

  int
  cli_error(std::string const& error, boost::optional<std::string> object = {})
  {
    elle::fprintf(std::cerr, "%s: command line error: %s\n", argv_0, error);
    auto const obj = object ? " " + *object : "";
    elle::fprintf(std::cerr,
                  "Try '%s%s --help' for more information.\n",
                  memo_exe, obj);
    return 2;
  }


  /// Return true if we found (and ran) the command.
  bool
  run_command(memo::cli::Memo& cli, std::vector<std::string>& args)
  {
    cli.command_line(args);
    bool res = false;
    memo::cli::Memo::Objects::map<memo::cli::mode_call,
                                  memo::cli::Memo,
                                  memo::cli::Memo>::value(
      cli, cli, args, res);
    return res;
  }

  void
  main_impl(std::vector<std::string>& args)
  {
    args.erase(args.begin());
    auto memo = memo::Memo{};
    auto&& cli = memo::cli::Memo(memo);
    if (args.empty() || elle::das::cli::is_option(args[0], memo::cli::options))
      elle::das::cli::call(cli, args, memo::cli::options);
    else if (!run_command(cli, args))
      elle::err<memo::cli::CLIError>("unknown object type: %s", args[0]);
  }

  void
  _main(std::vector<std::string>& args)
  {
    auto const url = elle::sprintf(
      "%s/crash/report",
      elle::os::getenv("MEMO_CRASH_REPORT_HOST", memo::beyond()));
    auto report_thread = memo::make_reporter_thread();
    memo::cli::check_broken_locale();
    memo::environ_check("MEMO");
    main_impl(args);
    if (report_thread)
      elle::reactor::wait(*report_thread);
  }
}

int
main(int argc, char** argv)
{
  namespace bfs = boost::filesystem;

  argv_0 = argv[0];
  memo_exe = (bfs::path(argv_0).parent_path() / "memo").string();
  if (boost::algorithm::ends_with(argv_0, ".exe"))
    memo_exe += ".exe";
  try
  {
    auto args = std::vector<std::string>(argv, argv + argc);
    elle::reactor::Scheduler s;
    elle::reactor::Thread main(s, "main", [&] { _main(args); });
    s.run();
  }
  catch (memo::cli::CLIError const& e)
  {
    return cli_error(e.what(), e.object());
  }
  catch (elle::das::cli::Error const& e)
  {
    return cli_error(e.what());
  }
  catch (elle::Error const& e)
  {
    elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
    if (memo::getenv("BACKTRACE", false))
      elle::fprintf(std::cerr, "%s\n", e.backtrace());
    return 1;
  }
  catch (bfs::filesystem_error const& e)
  {
    elle::fprintf(std::cerr, "%s: fatal error: %s\n", argv[0], e.what());
    return 1;
  }
  catch (elle::Exit const& e)
  {
    return e.return_code();
  }
}
