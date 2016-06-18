#include <elle/log.hh>

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

std::string program;
bool script_mode = false;
boost::optional<std::string> _as_user = {};
