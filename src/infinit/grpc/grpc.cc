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

#include <infinit/grpc/grpc.hh>

ELLE_LOG_COMPONENT("infinit.grpc");

namespace infinit
{
  namespace grpc
  {
    bool _serving = true;
    int _tasks = 0;
    std::mutex _stop_mutex;
    std::condition_variable _stop_cond;

    bool Task::proceed()
    {
      return _proceed;
    }

    Task::Task()
    {
      std::unique_lock<std::mutex> lock(_stop_mutex);
      if (!_serving)
        _proceed = false;
      else
      {
        _proceed = true;
        ++_tasks;
      }
    }

    Task::~Task()
    {
      if (_proceed)
      {
        std::unique_lock<std::mutex> lock(_stop_mutex);
        if (!--_tasks)
          _stop_cond.notify_all();
      }
    }

    void serve_grpc(infinit::model::Model& dht,
                    boost::optional<elle::reactor::filesystem::FileSystem&> fs,
                    std::string const& ep,
                    int* effective_port)
    {
      _serving = true;
      std::unique_ptr< ::grpc::Service> fs_service;
      if (fs)
        fs_service = filesystem_service(*fs);
      auto ds = doughnut_service(dht);
      ::grpc::ServerBuilder builder;
      builder.AddListeningPort(ep, ::grpc::InsecureServerCredentials(),
        effective_port);
      builder.RegisterService(ds.get());
      if (fs_service)
        builder.RegisterService(fs_service.get());
      auto server = builder.BuildAndStart();
       ELLE_TRACE("serving grpc on %s (effective %s)", ep,
         effective_port ? *effective_port : 0);
      elle::SafeFinally shutdown([&] {
          _serving = false;
          elle::reactor::background([&] {
              std::unique_lock<std::mutex> lock(_stop_mutex);
              while (_tasks)
                _stop_cond.wait(lock);
          });
      });
      elle::reactor::sleep();
    }
  }
}
