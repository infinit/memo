#include <reactor/network/exception.hh>
#include <reactor/network/tcp-server.hh>
#include <reactor/scheduler.hh>

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
      return 1;
    }
    reactor::Scheduler sched;
    reactor::Thread acceptor(
      sched, "acceptor", [&]
      {
        reactor::network::TCPServer server;
        server.listen(std::atoi(argv[1]));
        std::vector<std::unique_ptr<reactor::Thread>> client_threads;
        while (true)
        {
          std::shared_ptr<reactor::network::Socket> socket = server.accept();
          client_threads.emplace_back(
            new reactor::Thread(
              sched, "serve",
              [socket]
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
              }));
        }
      });
    sched.run();
  }
  catch (std::exception const& e)
  {
    std::cerr << "fatal error: " << e.what() << std::endl;
    return 1;
  }
}
