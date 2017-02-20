#include <grpc/grpc.h>

#include <boost/type_traits/function_traits.hpp>

#include <grpc++/channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>

#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>

#include <elle/Duration.hh>
#include <reactor/scheduler.hh>

#include <infinit/model/Endpoints.hh>
#include <infinit/model/Model.hh>
#include <infinit/grpc/kv.grpc.pb.h>

#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/ACB.hh>


ELLE_LOG_COMPONENT("infinit.grpc");


/* This is what we would do if we were using the sync API (which uses a thread pool)
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
     new reactor::Thread("process", [&] {
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
    class KVImpl
    {
    public:
      KVImpl(infinit::model::Model& model)
        : _model(model)
      {}
      ::BlockStatus
      Get(const ::Address& request);
      ::Status
      Set(const ::ModeBlock& request);

      ELLE_ATTRIBUTE(infinit::model::Model&, model);
    };

    ::BlockStatus
    KVImpl::Get(const ::Address& request)
    {
      ELLE_LOG("Get %s", request.address());
      ::BlockStatus res;
      auto addr = model::Address::from_string(request.address());
      auto block = _model.fetch(addr);
      auto* mblock = res.mutable_block();
      mblock->set_address(elle::sprintf("%s", addr));
      mblock->set_payload(block->data().string());
      if (auto* chb = dynamic_cast<model::doughnut::CHB*>(block.get()))
      {
        ::ConstantBlockData* cbd = mblock->mutable_constant_block();
        cbd->set_owner(elle::sprintf("%s", chb->owner()));
      }
      else if (auto* chb = dynamic_cast<model::blocks::ImmutableBlock*>(block.get()))
      {
        ELLE_WARN("unknown immutable block kind %s", elle::type_info(*chb));
        ::ConstantBlockData* cbd = mblock->mutable_constant_block();
        cbd->set_owner(std::string());
      }
      else if (auto* acb = dynamic_cast<model::blocks::ACLBlock*>(block.get()))
      {
        ::ACLBlockData* abd = mblock->mutable_acl_block();
        abd->set_version(acb->version());
        abd->set_world_read(acb->get_world_permissions().first);
        abd->set_world_write(acb->get_world_permissions().second);
        auto entries = acb->list_permissions(_model);
        for (auto const& e: entries)
        {
          ::ACL* p = abd->add_permissions();
          p->set_admin(e.admin);
          p->set_owner(e.owner);
          p->set_read(e.read);
          p->set_write(e.write);
          p->set_user(e.user->name());
        }
      }
      else if (auto* mb = dynamic_cast<model::blocks::MutableBlock*>(block.get()))
      {
        ::MutableBlockData* mbd = mblock->mutable_mutable_block();
        mbd->set_version(mb->version());
      }
      else
      {
        ELLE_ERR("unknown block type %s", elle::type_info(*block));
      }

      reactor::sleep(2_sec);
      res.mutable_status()->set_error(std::string());
      return res;
    }

    ::Status
    KVImpl::Set(const ::ModeBlock& request)
    {
      auto const& iblock = request.block();
      ELLE_LOG("Set %s", iblock.address());
      ::Status res;
      std::unique_ptr<model::blocks::Block> block;
      if (iblock.has_constant_block())
      {
        auto const& rc = iblock.constant_block();
        auto cblock = _model.make_block<model::blocks::ImmutableBlock>(
          iblock.payload(),
          rc.owner().empty() ?
            model::Address::null
            : model::Address::from_string(rc.owner()));
        block = std::move(cblock);
      }
      else if (iblock.has_mutable_block())
      {
        auto mblock = _model.make_block<model::blocks::MutableBlock>();
        mblock->data(iblock.payload());
        auto const& rm = iblock.mutable_block();
        block = std::move(mblock);
      }
      else if (iblock.has_acl_block())
      {
        auto ablock = _model.make_block<model::blocks::ACLBlock>();
        ablock->data(iblock.payload());
        auto const& ra = iblock.acl_block();
        ablock->set_world_permissions(/*ra.has_world_read() &&*/ ra.world_read(),
                                      /*ra.has_world_write() &&*/ ra.world_write());
        for (int i=0; i< ra.permissions_size(); ++i)
        {
          auto const& p = ra.permissions(i);
          auto user = _model.make_user(elle::Buffer(p.user()));
          ablock->set_permissions(*user,
                                  /*p.has_read() &&*/ p.read(),
                                  /*p.has_write() &&*/ p.write());
        }
        if (/*ra.has_version() &&*/ ra.version() >= 0)
          dynamic_cast<model::doughnut::ACB&>(*ablock).seal(ra.version());
        block = std::move(ablock);
      }
      else
        elle::err("unknown block kind in request");
      _model.store(std::move(block), (infinit::model::StoreMode)request.mode());

      res.set_error("not implemented");
      return res;
    }

    void serve_grpc(infinit::model::Model& dht, model::Endpoint ep)
    {
      ELLE_LOG("serving");
      KV::AsyncService async;
      KVImpl impl(dht);
      ::grpc::ServerBuilder builder;
      auto sep = ep.address().to_string() + ":" + std::to_string(ep.port());
      builder.AddListeningPort(sep, ::grpc::InsecureServerCredentials());
      builder.RegisterService(&async);
      auto cq = builder.AddCompletionQueue();
      auto server = builder.BuildAndStart();
      register_call(*cq, impl, async, &KVImpl::Get, &KV::AsyncService::RequestGet);
      register_call(*cq, impl, async, &KVImpl::Set, &KV::AsyncService::RequestSet);
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