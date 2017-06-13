#include <boost/program_options.hpp>

#include <elle/os/environ.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>

#include <crash-report/CrashReporter.hh>

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
    auto p = static_cast<int volatile*>(nullptr);
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
  auto const vm = [&]
    {
      auto res = po::variables_map{};
      po::store(po::parse_command_line(argc, argv, desc), res);
      po::notify(res);
      return res;
    }();
  if (vm.count("help"))
    std::cout << desc << std::endl;
  else
  {
    bool crash = vm.count("crash");
    auto const dumps = option_str(vm, "dumps");
    auto const server = option_str(vm, "server");
    auto const version =
      vm.count("version") ? option_str(vm, "version") : "test_version";
    // Enable crash handling.
    elle::os::setenv("INFINIT_CRASH_REPORT", "1");
    auto crash_reporter =
      std::make_unique<crash_report::CrashReporter>(server, dumps, version);
    if (crash)
      do_crash();
    elle::reactor::Scheduler sched;
    elle::reactor::Thread main_thread(sched, "main",
      [&crash_reporter]
      {
        crash_reporter->upload_existing();
      });
    sched.run();
  }
}
