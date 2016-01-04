#include <elle/utility/Move.hh>
#include <reactor/network/exception.hh>
#include <reactor/network/tcp-server.hh>
#include <reactor/scheduler.hh>

void echo(std::unique_ptr<reactor::network::Socket> socket)
{
  try
  {
    while (true)
    {
      elle::Buffer line = socket->read_until("\n");
      socket->write(line);
    }
  }
  catch (reactor::network::ConnectionClosed const&)
  {}
}

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: reactor_tcp_echo_server <port>" << std::endl;
      return 1;
    }
    reactor::Scheduler sched;
    reactor::Thread acceptor(sched, "acceptor", [&]
      {
        reactor::network::TCPServer server;
        server.listen(std::atoi(argv[1]));
        std::vector<std::unique_ptr<reactor::Thread>> client_threads;
        while (true)
        {
          std::unique_ptr<reactor::network::Socket> client =
            std::move(server.accept());
          client_threads.push_back(
            std::unique_ptr<reactor::Thread>(new reactor::Thread(
              sched, "serve",
              std::bind(&echo, elle::utility::move_on_copy(client)))));
        }
      });
    sched.run();
    return 0;
  }
  catch (std::exception const& e)
  {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }
}
