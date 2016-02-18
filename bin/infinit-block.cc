#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <elle/printf.hh>
#include <elle/serialization/binary.hh>
#include <elle/serialization/json.hh>

#include <reactor/scheduler.hh>

#include <infinit/model/doughnut/consensus/Paxos.hh>

int
main(int argc, char** argv)
{
  reactor::Scheduler sched;
  reactor::Thread t(
    sched, "infinit-block",
    [&]
    {
      for (int i = 1; i < argc; ++i)
      {
        boost::filesystem::ifstream f(argv[i]);
        if (!f.good())
          throw elle::Error(
            elle::sprintf("unable to open for reading: %s", argv[i]));
        elle::serialization::Context ctx;
        ctx.set<infinit::model::doughnut::Doughnut*>(nullptr);
        auto block = elle::serialization::binary::deserialize<
          infinit::model::doughnut::consensus::BlockOrPaxos>(f, true, ctx);
        elle::serialization::json::serialize(block, std::cout);
      }
    });
  sched.run();
}
