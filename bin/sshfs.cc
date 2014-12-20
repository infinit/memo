
#include <infinit/storage/sftp.hh>
#include <infinit/storage/Crypt.hh>
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
  if (argc != 5 && argc != 6)
  {
    std::cerr << "Usage: " << argv[0] <<" host host_path mount_point root [cryptkey]" << std::endl;
    exit(1);
  }
  
  infinit::storage::SFTP* sftp = new infinit::storage::SFTP(argv[1], argv[2]);
  infinit::storage::Storage* filter = nullptr;
  if (argc > 5)
    filter = new infinit::storage::Crypt(*sftp, argv[5], true);
  auto faith = elle::make_unique<infinit::model::faith::Faith>(filter?*filter:*sftp);
  auto fsops = elle::make_unique<infinit::filesystem::FileSystem>(argv[4], std::move(faith));
  auto fsopsPtr = fsops.get();
  fs = new reactor::filesystem::FileSystem(std::move(fsops), true);
  fsopsPtr->fs(fs);
  fs->mount(argv[3], {});
}


int main(int argc, char** argv)
{
  reactor::Scheduler sched;
  sched.signal_handle(SIGINT, sig_int);
  reactor::Thread t(sched, "main", [&] {main_scheduled(argc, argv);});
  sched.run();
  return 0;
}