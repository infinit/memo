#include <iostream>
#include <fstream>

#include <elle/factory.hh>

#include <reactor/filesystem.hh>
#include <reactor/thread.hh>
#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/faith/Faith.hh>


reactor::filesystem::FileSystem* fs;

static void sig_int()
{
  fs->unmount();
}


void main_scheduled(int argc, char** argv)
{
  if (argc < 4)
  {
    std::cerr << "Usage: " << argv[0] <<"mount_point root STORAGE STORAGEOPTS..." << std::endl;
    exit(1);
  }
  std::string storage_name = argv[3];
  std::vector<std::string> args;
  for (int i=4; i<argc; ++i)
    args.push_back(argv[i]);
  std::unique_ptr<infinit::storage::Storage> s =
    elle::Factory<infinit::storage::Storage>::instantiate(storage_name, args);
  auto faith = elle::make_unique<infinit::model::faith::Faith>(*s.release());
  auto fsops = elle::make_unique<infinit::filesystem::FileSystem>(argv[2], std::move(faith));
  auto fsopsPtr = fsops.get();
  fs = new reactor::filesystem::FileSystem(std::move(fsops), true);
  fsopsPtr->fs(fs);
  fs->mount(argv[1], {});
}



int main(int argc, char** argv)
{
  reactor::Scheduler sched;
  sched.signal_handle(SIGINT, sig_int);
  reactor::Thread t(sched, "main", [&] {main_scheduled(argc, argv);});
  sched.run();
  return 0;
}
