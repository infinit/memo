#include <grpc/grpc.h>

#include <boost/type_traits/function_traits.hpp>

#include <grpc++/channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>

#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>

#include <infinit/model/Endpoints.hh>
#include <infinit/model/Model.hh>
#include <infinit/grpc/kv.grpc.pb.h>

ELLE_LOG_COMPONENT("infinit.grpc");


/*
class KVImpl: public KV::Service
{
public:
  ::grpc::Status Get(::grpc::ServerContext* context, const ::Address* request, ::BlockStatus* response)
  {
    ELLE_LOG("Get %s", request->address());
    response->mutable_status()->set_error("not implemented");
    return grpc::Status::OK;
  }
  ::grpc::Status Set(::grpc::ServerContext* context, const ::Block* request, ::Status* response)
  {
    ELLE_LOG("Set %s", request->address());
    response->set_error("not implemented");
    return grpc::Status::OK;
  }
};*/

class KVImpl
{
public:
  ::BlockStatus Get(const ::Address& request)
  {
    ELLE_LOG("Get %s", request.address());
    ::BlockStatus res;
    res.mutable_status()->set_error("not implemented");
    return res;
  }
  ::Status Set(const ::Block& request)
  {
    ELLE_LOG("Set %s", request.address());
    ::Status res;
    res.set_error("not implemented");
    return res;
  }
};

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
  /* FINME the damm thing fails on member functions
   typedef typename
   std::remove_reference<
     typename std::remove_const<
       typename boost::mpl::at_c<typename boost::function_traits<F>::parameter_types,1>::type
       >::type
     >::type
   request_type;
   typedef typename
   boost::mpl::at_c<typename boost::function_traits<F>::parameter_types,0>::type
   impl_type;
   typedef typename
   boost::mpl::at_c<typename boost::function_traits<R>::parameter_types,0>::type
   service_type;
   typedef typename
   boost::function_traits<R>::result_type
   reply_type;
   */
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
   CallManager(::grpc::ServerContext& ctx,
               ::grpc::ServerCompletionQueue& queue,
               impl_type& impl, service_type& service,
               F backend, R initiator)
   : _ctx(ctx)
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
     // queue new request
     new CallManager<F, R>(_ctx, _queue, _impl, _service, _backend, _initiator);
     // request received
     reply_type res = (_impl.*_backend)(_request);
     _reply.Finish(res, ::grpc::Status::OK, this);
   }
 private:
   ::grpc::ServerContext& _ctx;
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
void register_call(::grpc::ServerContext& ctx,
                   ::grpc::ServerCompletionQueue& queue,
                   B& impl, S& service,
                   F backend, I initiator)
{
  new CallManager<F, I>(ctx, queue, impl, service, backend, initiator);
}

namespace infinit
{
  namespace grpc
  {
    void serve_grpc(infinit::model::Model& dht, model::Endpoint ep)
    {
      ELLE_LOG("serving");
      KV::AsyncService async;
      KVImpl impl;
      ::grpc::ServerBuilder builder;
      auto sep = ep.address().to_string() + ":" + std::to_string(ep.port());
      builder.AddListeningPort(sep, ::grpc::InsecureServerCredentials());
      builder.RegisterService(&async);
      auto cq = builder.AddCompletionQueue();
      auto server = builder.BuildAndStart();
      ::grpc::ServerContext ctx;
      register_call(ctx, *cq, impl, async, &KVImpl::Get, &KV::AsyncService::RequestGet);
      register_call(ctx, *cq, impl, async, &KVImpl::Set, &KV::AsyncService::RequestSet);
      while (true)
      {
        void* tag;
        bool ok;
        ELLE_LOG("next...");
        auto res = cq->Next(&tag, &ok);
        ELLE_LOG("...next");
        if (!res)
          break;
        ((BaseCallManager*)tag)->proceed();
      }
      ELLE_LOG("leaving");
    }
  }
}