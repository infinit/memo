#include <thread>

#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/grpc++.h>
#include <grpc++/impl/codegen/service_type.h>
#include <grpc++/security/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>

#include <elle/reactor/scheduler.hh>

#include <memo/grpc/grpc.hh>

ELLE_LOG_COMPONENT("memo.grpc");

namespace memo
{
  namespace grpc
  {
    bool
    Server::Task::proceed() const
    {
      return this->_proceed;
    }

    Server::Task::Task(Server& server)
      : _server(server)
    {
      std::unique_lock<std::mutex> lock(this->_server._stop_mutex);
      if (!this->_server._serving)
        this->_proceed = false;
      else
      {
        this->_proceed = true;
        ++this->_server._tasks;
      }
    }

    Server::Task::~Task()
    {
      if (this->_proceed)
      {
        std::unique_lock<std::mutex> lock(this->_server._stop_mutex);
        if (!--this->_server._tasks)
          this->_server._stop_cond.notify_all();
      }
    }

    Server::Task
    Server::task()
    {
      return Task(*this);
    }

    Server::Server()
      : _serving(false)
      , _tasks(0)
    {}

    void
    Server::operator ()(::grpc::Service* service,
                        std::string const& ep,
                        int* effective_port)
    {
      this->_serving = true;
      ::grpc::ServerBuilder builder;
      builder.AddListeningPort(ep, ::grpc::InsecureServerCredentials(),
        effective_port);
      builder.RegisterService(service);
      auto server = builder.BuildAndStart();
       ELLE_TRACE("serving grpc on %s (effective %s)", ep,
                  effective_port ? *effective_port : 0);
      elle::SafeFinally shutdown([&] {
          this->_serving = false;
          elle::reactor::background([&] {
              std::unique_lock<std::mutex> lock(this->_stop_mutex);
              while (this->_tasks)
                this->_stop_cond.wait(lock);
          });
      });
      elle::reactor::sleep();
    }
  }
}
