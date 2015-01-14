#include <iostream>
#include <fstream>
#include <elle/serialization/json/SerializerIn.hh>
#include <aws/S3.hh>

#include <reactor/filesystem.hh>
#include <reactor/thread.hh>
#include <reactor/scheduler.hh>

#include <infinit/filesystem/filesystem.hh>
#include <infinit/storage/S3.hh>
#include <infinit/model/faith/Faith.hh>

reactor::filesystem::FileSystem* fs;

static void sig_int()
{
  fs->unmount();
}


void main_scheduled(int argc, char** argv)
{
  if (argc != 4)
  {
    std::cerr << "Usage: " << argv[0] <<" credentials_file mount_point root" << std::endl;
    exit(1);
  }
  std::ifstream is(argv[1]);
  elle::serialization::json::SerializerIn input(is);
  aws::Credentials creds(input);
  creds.skew(boost::posix_time::time_duration());
  auto s3 = elle::make_unique<aws::S3>(creds);
  auto faith = elle::make_unique<infinit::model::faith::Faith>
    (elle::make_unique<infinit::storage::S3>(std::move(s3)));
  auto fsops = elle::make_unique<infinit::filesystem::FileSystem>
    (infinit::model::Address::from_string(argv[3]), std::move(faith));
  auto fsopsPtr = fsops.get(); // FIXME: ffs
  fs = new reactor::filesystem::FileSystem(std::move(fsops), true);
  fsopsPtr->fs(fs); // FIXME: >.<
  fs->mount(argv[2], {});
}


int main(int argc, char** argv)
{
  reactor::Scheduler sched;
  sched.signal_handle(SIGINT, sig_int);
  reactor::Thread t(sched, "main", [&] {main_scheduled(argc, argv);});
  sched.run();
  return 0;
}
