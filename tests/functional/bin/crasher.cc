#include <boost/program_options.hpp>

#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>

#include <crash_reporting/CrashReporter.hh>

namespace po = boost::program_options;

namespace
{
  std::string
  option_str(po::variables_map const& vm, std::string const& name)
  {
    if (vm.count(name))
      return vm[name].as<std::string>();
    else
    {
      std::cerr << "require option: " << name << std::endl;
      abort();
    }
  }

  void
  do_crash()
  {
    int* p = nullptr;
    *p = 1;
  }
}

int
main(int argc, char** argv)
{
  auto desc = po::options_description("Crasher options");
  desc.add_options()
    ("crash", "Crash!")
    ("dumps", po::value<std::string>(), "Crash dump location")
    ("help", "Help!")
    ("server", po::value<std::string>(), "Server to upload to")
    ("version", po::value<std::string>(), "Version to send to server")
  ;
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  if (vm.count("help"))
  {
    std::cout << desc << std::endl;
    return 1;
  }
  bool crash = vm.count("crash");
  auto const dumps = option_str(vm, "dumps");
  auto const server = option_str(vm, "server");
  auto const version =
    vm.count("version") ? option_str(vm, "version") : "test_version";
  auto crash_reporter =
    std::make_unique<crash_reporting::CrashReporter>(server, dumps, version);
  if (crash)
    do_crash();
  elle::reactor::Scheduler sched;
  elle::reactor::Thread main_thread(
    sched,
    "main",
    [&crash_reporter]
    {
      crash_reporter->upload_existing();
    });
  sched.run();
}
