#include <infinit/grpc/grpc.hh>
#include <grpc/grpc.h>
#include <grpc++/grpc++.h>
#include <grpc++/impl/codegen/service_type.h>

#include <boost/type_traits/function_traits.hpp>

#include <grpc++/channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>

#include <elle/Duration.hh>
#include <elle/os/environ.hh>

#include <elle/reactor/scheduler.hh>
#include <elle/reactor/network/SocketOperation.hh>

#include <infinit/model/Endpoints.hh>

ELLE_LOG_COMPONENT("infinit.grpc");


class BaseCallManager
{
public:
  virtual ~BaseCallManager() {}
  virtual void proceed() = 0;
};

template<typename F> struct member_function_traits
{
};
template<typename R, typename O, typename A>
struct member_function_traits<R (O::*)(A)>
{
  using result_type = R;
  using object_type = O;
  using parameter_type = A;
};

template<typename R, typename O, typename A, typename... B>
struct member_function_traits<R (O::*)(A, B...)>
{
  using result_type = R;
  using object_type = O;
  using parameter_type = A;
};

template<typename F, typename R>
class CallManager: public BaseCallManager
{
public:
   typedef typename
   std::remove_const<
     typename std::remove_reference<
       typename member_function_traits<F>::parameter_type
     >::type
     >::type request_type;
   typedef typename member_function_traits<R>::object_type
   service_type;
   typedef typename member_function_traits<F>::object_type
   impl_type;
   typedef typename member_function_traits<F>::result_type
   reply_type;
   CallManager(::grpc::ServerCompletionQueue& queue,
               impl_type& impl, service_type& service,
               F backend, R initiator)
   : _ctx()
   , _queue(queue)
   , _impl(impl)
   , _service(service)
   , _backend(backend)
   , _initiator(initiator)
   , _reply(&_ctx)
   , _finalizing(false)
   {
     (service.*initiator)(&_ctx, &_request, &_reply, &_queue, &_queue, this);
   }
   void proceed() override
   {
     if (_finalizing)
     {
       delete this;
       return;
     }
     _finalizing = true;
     // queue new request
     new CallManager<F, R>(_queue, _impl, _service, _backend, _initiator);
     // request received
     // FIXME track me
     new elle::reactor::Thread("process", [&] {
         reply_type res = (_impl.*_backend)(_request);
         _reply.Finish(res, ::grpc::Status::OK, this);
     }, true);
   }
 private:
   ::grpc::ServerContext _ctx;
   ::grpc::ServerCompletionQueue& _queue;
   impl_type& _impl;
   service_type& _service;
   F _backend;
   R _initiator;
   int64_t _id;
   request_type _request;
   ::grpc::ServerAsyncResponseWriter<reply_type> _reply;
   bool _finalizing;
};

template<typename F, typename I, typename B, typename S>
void register_call(::grpc::ServerCompletionQueue& queue,
                   B& impl, S& service,
                   F backend, I initiator)
{
  new CallManager<F, I>(queue, impl, service, backend, initiator);
}

namespace infinit
{
  namespace grpc
  {
    void serve_grpc(infinit::model::Model& dht,
                    boost::optional<elle::reactor::filesystem::FileSystem&> fs,
                    model::Endpoint ep)
    {
      ELLE_TRACE("serving grpc on %s", ep);
      std::unique_ptr< ::grpc::Service> fs_service;
      if (fs)
        fs_service = filesystem_service(*fs);
      auto ds = doughnut_service(dht);
      auto kv = kv_service(dht);
      ::grpc::ServerBuilder builder;
      auto sep = ep.address().to_string() + ":" + std::to_string(ep.port());
      builder.AddListeningPort(sep, ::grpc::InsecureServerCredentials());
      builder.RegisterService(ds.get());
      builder.RegisterService(kv.get());
      if (fs_service)
        builder.RegisterService(fs_service.get());
      auto server = builder.BuildAndStart();
      elle::reactor::sleep();
    }

    static
    void serve_grpc_async(infinit::model::Model& dht, model::Endpoint ep)
    {
#ifdef INFINIT_LINUX
      elle::os::setenv("GRPC_EPOLL_SYMBOL", "reactor_epoll_pwait", 1);
      ELLE_TRACE("serving grpc on %s", ep);
      //KV::AsyncService async;
      //KVImpl impl(dht);
      ::grpc::ServerBuilder builder;
      auto sep = ep.address().to_string() + ":" + std::to_string(ep.port());
      builder.AddListeningPort(sep, ::grpc::InsecureServerCredentials());
      //builder.RegisterService(&async);
      auto cq = builder.AddCompletionQueue();
      auto server = builder.BuildAndStart();
      /*
      register_call(*cq, impl, async, &KVImpl::Get,
                    &KV::AsyncService::RequestGet);
      register_call(*cq, impl, async, &KVImpl::Insert,
                    &KV::AsyncService::RequestInsert);
      register_call(*cq, impl, async, &KVImpl::Update,
                    &KV::AsyncService::RequestUpdate);
      register_call(*cq, impl, async, &KVImpl::Remove,
                    &KV::AsyncService::RequestRemove);
                    */
      /* Pay attention: when this thread gets terminated, it will most likely
      * occur in reactor_epoll_wait. This function will intercept the exception
      * and invoke the reactor_epoll_interrupt callback.
      * In theory we should be able to just call `cq->Shutdown()` from
      * that callback, which should make `cq->Next()` return, but it doesn't
      * work. I tried synchronously, asynchronously, and from a different
      * OS thread, the Shutdown call has no effect whatsofuckingever.
      * So instead we poll using AsyncNext with a 1s delay and detect the
      * shutdown condition ourselve.
      */
      bool interrupted = false;
      elle::reactor::network::epoll_interrupt_callback([&] {
          ELLE_DEBUG("epoll interrupted");
          if (interrupted)
            return;
          interrupted = true;
          cq->Shutdown();
      }, elle::reactor::scheduler().current());
      while (true)
      {
        void* tag;
        bool ok;
        ELLE_DUMP("next...");
        auto res = cq->AsyncNext(&tag, &ok,
          std::chrono::system_clock::now() + std::chrono::seconds(1));
        ELLE_DUMP("...next %s %s", res, interrupted);
        if (interrupted)
        {
          cq->Shutdown();
          break;
        }
        if (res == ::grpc::CompletionQueue::SHUTDOWN)
          break;
        else if (res == ::grpc::CompletionQueue::TIMEOUT)
          continue;
        ((BaseCallManager*)tag)->proceed();
      }
      ELLE_TRACE("leaving serve_grpc");
      elle::reactor::network::epoll_interrupt_callback(std::function<void()>(),
                                                       elle::reactor::scheduler().current());
#else
      elle::err("serve_grpc_async not implemented on this platform");
#endif
    }
  }
}