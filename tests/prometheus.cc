#include <string>

#include <elle/log.hh>
#include <elle/test.hh>

ELLE_LOG_COMPONENT("infinit.tests.prometheus");

#include "DHT.hh"

void run(int num_servers = 30)
{
  ELLE_LOG("running");
  auto const k = elle::cryptography::rsa::keypair::generate(512);

  auto make_kouncil = [](infinit::model::doughnut::Doughnut& dht,
                         std::shared_ptr<infinit::model::doughnut::Local> local)
  {
    return std::make_unique<infinit::overlay::kouncil::Kouncil>(&dht, local);
  };

  auto servers = std::vector<DHT>{};
  auto new_server = [&]{
    auto const n = servers.size();
    ELLE_LOG("create server %s", n)
    servers.emplace_back(
      ::id = special_id(n + 1),
      ::keys = k,
      ::make_overlay = make_kouncil,
      ::storage = std::make_unique<infinit::storage::Memory>());
  };

  new_server();
  for (int i = 0; i < num_servers; ++i)
  {
    elle::reactor::sleep(1_sec);
    new_server();
    discover(servers[0], servers.back(), false, false, true, true);
  }
  elle::reactor::sleep(5_sec);
  // And then kill one after the other.
  while (!servers.empty())
  {
    elle::reactor::sleep(1_sec);
    ELLE_LOG("kill server %s", servers.size() - 1);
    servers.pop_back();
  }
}

// FIXME: we should not use our main with Boost.Test.
int main(int argc, const char* argv[])
{
  int num_servers = 1 < argc ? std::stoi(argv[1]) : 5;
  ELLE_LOG("running with %s servers", num_servers);
  elle::reactor::Scheduler sched;
  ELLE_TEST_HANDLE_SIGALRM(sched, prometheus);
  elle::reactor::Thread main(
    sched, "main",
    [&]
    {
      ELLE_LOG("starting test: prometheus")
        run(num_servers);
    });
  try
  {
    sched.run();
  }
  catch (elle::Error const& e)
  {
    ELLE_ERR("exception escaped test prometheus: %s",
             BOOST_PP_STRINGIZE(Name), e);
    ELLE_ERR("%s", e.backtrace());
    throw;
  }
}
