#pragma once

#include <elle/reactor/fwd.hh>
#include <elle/attribute.hh>

#include <boost/optional.hpp>

#include <memo/model/Endpoints.hh>
#include <memo/model/Model.hh>

namespace grpc
{
  class Service;
}

namespace memo
{
  namespace grpc
  {
    /// Wrap a gRPC server and ensure proper initialization and shutdown.
    ///
    /// Server::task returns a Server::Task to be kept alive while performing
    /// gRPC operation.
    ///
    /// ```
    /// auto task = service.server().task();
    /// if (!task.proceed())
    ///   return ::grpc::Status(::grpc::INTERNAL, "server is shuting down");
    /// elle::reactor::scheduler().mt_run<void>(
    ///   "proceed gRPC",
    ///   []
    ///   {
    ///     // XXX.
    ///   });
    /// ```
    class Server
    {
    public:
      Server();

    public:
      /// gRPC tasks (invoked by grpc callbacks) should acquire a Task
      /// from the callback thread, and abort if proceed() returns False.
      class Task
      {
      public:
        Task(Server&);
        ~Task();
      public:
        bool
        proceed() const;
      private:
        Server& _server;
        bool _proceed;
      };

    public:
      /// Create the gRPC server and wait until termination.
      ///
      /// The reactor::Thread will stay alive until all registered tasks are
      /// over.
      void
      operator ()(::grpc::Service* service,
                  std::string const& ep,
                  int* effective_port = nullptr);

    private:
      ELLE_ATTRIBUTE(bool, serving);
      ELLE_ATTRIBUTE(int, tasks);
      ELLE_ATTRIBUTE(std::mutex, stop_mutex);
      ELLE_ATTRIBUTE(std::condition_variable, stop_cond);

    public:
      Task
      task();

      friend Task;
    };
  }
}
