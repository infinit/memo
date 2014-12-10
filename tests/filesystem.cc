#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>

#include <infinit/storage/Storage.hh>
#include <infinit/storage/Memory.hh>

#include <infinit/model/faith/Faith.hh>

namespace ifs = infinit::filesystem;
namespace rfs = reactor::filesystem;

infinit::storage::Storage* storage;


reactor::filesystem::FileSystem* fs;

static void sig_int()
{
  fs->unmount();
}

static void main_scheduled(int argc, char** argv)
{
  storage = new infinit::storage::Memory();
  auto model = elle::make_unique<infinit::model::faith::Faith>(*storage);

  std::string mountpoint(argv[2]);
  std::unique_ptr<ifs::FileSystem> ops = elle::make_unique<ifs::FileSystem>(
    argv[3], std::move(model));
  ifs::FileSystem* ops_ptr = ops.get();
  fs = new reactor::filesystem::FileSystem(std::move(ops), true);
  ops_ptr->fs(fs);
  fs->mount(mountpoint, {"", "-o", "big_writes"}); // {"", "-d" /*, "-o", "use_ino"*/});
}

int main(int argc, char** argv)
{
  reactor::Scheduler sched;
  sched.signal_handle(SIGINT, sig_int);
  reactor::Thread t(sched, "main", [&] {main_scheduled(argc, argv);});
  sched.run();
  return 0;
}
