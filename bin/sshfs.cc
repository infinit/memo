#include <infinit/storage/sftp.hh>
#include <infinit/storage/Crypt.hh>
#include <reactor/filesystem.hh>
#include <reactor/thread.hh>
#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/model/faith/Faith.hh>
#include <infinit/version.hh>

reactor::filesystem::FileSystem* fs;

# define INFINIT_ELLE_VERSION elle::Version(INFINIT_MAJOR,   \
                                            INFINIT_MINOR,   \
                                            INFINIT_SUBMINOR)


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
  std::unique_ptr<infinit::storage::Storage> storage =
    elle::make_unique<infinit::storage::SFTP>(argv[1], argv[2]);
  if (argc > 5)
    storage = elle::make_unique<infinit::storage::Crypt>
      (std::move(storage), argv[5], true);
  auto faith =
    elle::make_unique<infinit::model::faith::Faith>(std::move(storage),
                                                    INFINIT_ELLE_VERSION);
  auto fsops = elle:: make_unique<infinit::filesystem::FileSystem>
    ("default-volume", std::move(faith));
  fs = new reactor::filesystem::FileSystem(std::move(fsops), true);
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
