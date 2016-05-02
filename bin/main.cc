#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit");

#include <main.hh>

reactor::Thread::unique_ptr
make_stat_update_thread(infinit::User const& self,
                        infinit::Network& network,
                        infinit::model::doughnut::Doughnut& model)
{
  model.local()->storage()->register_notifier([&] {
      try
      {
        network.notify_storage(self, model.id());
      }
      catch (elle::Error const& e)
      {
        ELLE_WARN("Error notifying storage size change: %s", e);
      }
    });
  return reactor::every(
    60_min,
    "periodic storage stat updater", [&] {
      ELLE_TRACE_SCOPE("push storage stats to %s", beyond());
      try
      {
        network.notify_storage(self, model.id());
      }
      catch (elle::Error const& e)
      {
        ELLE_WARN("Error notifying storage size change: %s", e);
      }
    });
}

std::string program;
bool script_mode = false;
boost::optional<std::string> _as_user = {};
