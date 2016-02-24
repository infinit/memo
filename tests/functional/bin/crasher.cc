#include <boost/program_options.hpp>

#include <reactor/scheduler.hh>
#include <reactor/thread.hh>

#include <crash_reporting/CrashReporter.hh>

namespace po = boost::program_options;

static
std::string
option_str(po::variables_map const& vm, std::string const& name)
{
  if (!vm.count(name))
  {
    std::cerr << "require option: " << name << std::endl;
    abort();
  }
  return vm[name].as<std::string>();
}

static
void
do_crash()
{
  int* p = NULL;
  *p = 1;
}

int
main(int argc, char** argv)
{
  po::options_description desc("Crasher options");
  desc.add_options()
    ("crash", "Crash!")
    ("dumps", po::value<std::string>(), "Crash dump location")
    ("help", "Help!")
    ("server", po::value<std::string>(), "Server to upload to")
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
  std::string dumps = option_str(vm, "dumps");
  std::string server = option_str(vm, "server");
  auto crash_reporter =
    elle::make_unique<crash_reporting::CrashReporter>(server, dumps);
  if (crash)
    do_crash();
  reactor::Scheduler sched;
  reactor::Thread main_thread(
    sched,
    "main",
    [&crash_reporter]
    {
      crash_reporter->upload_existing();
    });
  sched.run();
  return 0;
}
