#include <elle/log.hh>

#include <reactor/FDStream.hh>

ELLE_LOG_COMPONENT("infinit");

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
    auto stdin_stream = elle::IOStream(input.istreambuf());
  #endif
  }
}

std::string program;
bool script_mode = false;
boost::optional<std::string> _as_user = {};
