#include <infinit/grpc/kv.grpc.pb.h>

#include <grpc++/grpc++.h>

extern "C" int reactor_epoll_pwait() {}
extern "C" int reactor_epoll_wait() {}

int main(int argc, char** argv)
{
  auto chan = grpc::CreateChannel(
      argv[1], grpc::InsecureChannelCredentials());
  auto stub = KV::NewStub(chan);
  grpc::ClientContext context;
  ::Address req;
  ::BlockStatus repl;
  stub->Get(&context, req, &repl);
  std::cerr << repl.status().error() << std::endl;
}