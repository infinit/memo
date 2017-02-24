#include <infinit/grpc/grpc.hh>
#include <grpc/grpc.h>

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

# include <elle/athena/paxos/Client.hh>

#include <infinit/model/Endpoints.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/Model.hh>

#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>

#include <infinit/grpc/kv.grpc.pb.h>

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
    class KVImpl
    {
    public:
      KVImpl(infinit::model::Model& model)
        : _model(model)
      {}
      ::BlockStatus
      Get(const ::Address& request);
      ::Status
      Insert(const ::Block& request);
      ::Status
      Update(const ::Block& request);
      ::Status
      Remove(const ::Address& request);

      ::Status
      Set(const ::Block& request, infinit::model::StoreMode mode);
      ELLE_ATTRIBUTE(infinit::model::Model&, model);
    };

    // This is what we would do if we were using the sync API (which uses a thread pool)
    class KVBounce: public KV::Service
    {
    public:
      KVBounce(KVImpl& impl, elle::reactor::Scheduler& sched)
      : _impl(impl)
      , _sched(sched)
      {}
      ::grpc::Status Get(::grpc::ServerContext* context, const ::Address* request, ::BlockStatus* response)
      {
        _sched.mt_run<void>("Get", [&] {
            *response = std::move(_impl.Get(*request));
        });
        return ::grpc::Status::OK;
      }
      ::grpc::Status Insert(::grpc::ServerContext* context, const ::Block* request, ::Status* response)
      {
        _sched.mt_run<void>("Insert", [&] {
            *response = std::move(_impl.Insert(*request));
        });
        return ::grpc::Status::OK;
      }
      ::grpc::Status Update(::grpc::ServerContext* context, const ::Block* request, ::Status* response)
      {
        _sched.mt_run<void>("Update", [&] {
            *response = std::move(_impl.Update(*request));
        });
        return ::grpc::Status::OK;
      }
      ::grpc::Status Remove(::grpc::ServerContext* context, const ::Address * request, ::Status* response)
      {
        _sched.mt_run<void>("Remove", [&] {
            *response = std::move(_impl.Remove(*request));
        });
        return ::grpc::Status::OK;
      }
      KVImpl& _impl;
      elle::reactor::Scheduler& _sched;
    };

    bool exception_handler(::Status& res, std::function<void()> f)
    {
      try
      {
        f();
        ELLE_TRACE("no exception encountered");
        return true;
      }
      catch (model::MissingBlock const& err)
      {
        res.set_error(ERROR_MISSING_BLOCK);
        res.set_message(err.what());
      }
      catch (model::doughnut::ValidationFailed const& err)
      {
        res.set_error(ERROR_VALIDATION_FAILED);
        res.set_message(err.what());
      }
      catch (elle::athena::paxos::TooFewPeers const& err)
      {
        res.set_error(ERROR_TOO_FEW_PEERS);
        res.set_message(err.what());
      }
      catch (model::doughnut::Conflict const& err)
      {
        res.set_error(ERROR_CONFLICT);
        res.set_message(err.what());
        if (auto* mb = dynamic_cast<model::blocks::MutableBlock*>(err.current().get()))
          res.set_version(mb->version());
      }
      catch (elle::Error const& err)
      {
        ELLE_TRACE("generic error handler for %s: %s",
          elle::type_info(err), err);
        // FIXME
        if (std::string(err.what()).find("no peer available for") == 0)
          res.set_error(ERROR_NO_PEERS);
        else
          res.set_error(ERROR_OTHER);
        res.set_message(err.what());
      }
      ELLE_TRACE("an exception was encountered");
      return false;
    }

    ::BlockStatus
    KVImpl::Get(const ::Address& request)
    {
      ELLE_LOG("Get %s", request.address());
      ::BlockStatus res;
      infinit::model::Address addr;
      if (request.address().find("NB:") == 0)
      {
        addr = infinit::model::doughnut::NB::address(
          dynamic_cast<model::doughnut::Doughnut&>(_model).keys().K(),
          request.address().substr(3),
          _model.version());
      }
      else
        addr = model::Address::from_string(request.address());
      res.mutable_status()->set_address(elle::sprintf("%s", addr));
      std::unique_ptr<model::blocks::Block> block;
      if (!exception_handler(*res.mutable_status(),
        [&] {  block = _model.fetch(addr); }))
        return res;

      auto* mblock = res.mutable_block();
      mblock->set_address(elle::sprintf("%s", addr));
      mblock->set_payload(block->data().string());
      if (auto* nb = dynamic_cast<model::doughnut::NB*>(block.get()))
      {
        ::NamedBlockData* nbd = mblock->mutable_named_block();
        nbd->set_name(nb->name());
        auto j = elle::serialization::json::serialize(*nb->owner());
        auto user = _model.make_user(j);
        nbd->set_owner(user->name());
      }
      else if (auto* chb = dynamic_cast<model::doughnut::CHB*>(block.get()))
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

      res.mutable_status()->set_error(ERROR_OK);
      res.mutable_status()->set_message(std::string());
      return res;
    }

    ::Status
    KVImpl::Insert(const ::Block& request)
    {
      return Set(request, infinit::model::STORE_INSERT);
    }

    ::Status
    KVImpl::Update(const ::Block& request)
    {
      return Set(request, infinit::model::STORE_UPDATE);
    }

    ::Status
    KVImpl::Set(const ::Block& request, infinit::model::StoreMode mode)
    {
      auto const& iblock = request;
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
        if (mode == infinit::model::STORE_INSERT)
        {
          auto mblock = _model.make_block<model::blocks::MutableBlock>();
          block = std::move(mblock);
        }
        else
        {
          if (!exception_handler(res, [&] {
              block = _model.fetch(model::Address::from_string(iblock.address()));
          }))
          return res;
        }
        // There is no API to seal a MutableBlock with a given version, so
        // make the conflict version check ourselve.
        auto vin = iblock.mutable_block().version();
        auto vcurr = dynamic_cast<model::blocks::MutableBlock&>(*block).version();
        if (vin != 0 && vin <= vcurr)
        {
          res.set_error(ERROR_CONFLICT);
          res.set_message(elle::sprintf("version conflict, current=%s, target=%s", vcurr, vin));
          res.set_version(vcurr);
          return res;
        }
        dynamic_cast<model::blocks::MutableBlock&>(*block).data(iblock.payload());
      }
      else if (iblock.has_acl_block())
      {
        model::blocks::ACLBlock* ablock;
        if (mode == infinit::model::STORE_INSERT)
        {
          auto ab = _model.make_block<model::blocks::ACLBlock>();
          ablock = ab.get();
          block = std::move(ab);
        }
        else
        {
          if (!exception_handler(res, [&] {
              block = _model.fetch(model::Address::from_string(iblock.address()));
          }))
            return res;
          ablock = dynamic_cast<model::blocks::ACLBlock*>(block.get());
          ELLE_ASSERT(ablock);
        }
        dynamic_cast<model::blocks::MutableBlock&>(*block).data(iblock.payload());
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
        if (/*ra.has_version() &&*/ ra.version() > 0)
          dynamic_cast<model::doughnut::ACB&>(*ablock).seal(ra.version());
      }
      else if (iblock.has_named_block())
      {
        auto const& rn = iblock.named_block();
        if (rn.owner().empty())
        {
          block.reset(new model::doughnut::NB(
            dynamic_cast<model::doughnut::Doughnut&>(_model),
            rn.name(),
            iblock.payload()));
        }
        else
        {
          auto user = _model.make_user(elle::Buffer(rn.owner()));
          block.reset(new model::doughnut::NB(
            dynamic_cast<model::doughnut::Doughnut&>(_model),
            std::make_shared<elle::cryptography::rsa::PublicKey>(
              dynamic_cast<model::doughnut::User&>(*user).key()),
            rn.name(),
            iblock.payload()));
        }
      }
      else
        elle::err("unknown block kind in request");
      res.set_address(elle::sprintf("%s", block->address()));
      res.set_error(ERROR_OK);
      res.set_message(std::string());
      exception_handler(res,
        [&] {
          _model.store(std::move(block), mode);
        });
      return res;
    }

    ::Status
    KVImpl::Remove(::Address const& address)
    {
      ::Status res;
      res.set_error(ERROR_OK);
      res.set_message(std::string());
      exception_handler(res,
        [&] {
        _model.remove(model::Address::from_string(address.address()));
      });
      return res;
    }

    void serve_grpc(infinit::model::Model& dht,
                    boost::optional<elle::reactor::filesystem::FileSystem&> fs,
                    model::Endpoint ep)
    {
      ELLE_TRACE("serving grpc on %s", ep);
      KVImpl impl(dht);
      KVBounce service(impl, elle::reactor::scheduler());
      std::unique_ptr< ::grpc::Service> fs_service;
      if (fs)
        fs_service = filesystem_service(*fs);
      ::grpc::ServerBuilder builder;
      auto sep = ep.address().to_string() + ":" + std::to_string(ep.port());
      builder.AddListeningPort(sep, ::grpc::InsecureServerCredentials());
      builder.RegisterService(&service);
      if (fs_service)
        builder.RegisterService(fs_service.get());
      auto server = builder.BuildAndStart();
      elle::reactor::sleep();
    }

    static
    void serve_grpc_async(infinit::model::Model& dht, model::Endpoint ep)
    {
      elle::os::setenv("GRPC_EPOLL_SYMBOL", "reactor_epoll_pwait", 1);
      ELLE_TRACE("serving grpc on %s", ep);
      KV::AsyncService async;
      KVImpl impl(dht);
      ::grpc::ServerBuilder builder;
      auto sep = ep.address().to_string() + ":" + std::to_string(ep.port());
      builder.AddListeningPort(sep, ::grpc::InsecureServerCredentials());
      builder.RegisterService(&async);
      auto cq = builder.AddCompletionQueue();
      auto server = builder.BuildAndStart();
      register_call(*cq, impl, async, &KVImpl::Get,
                    &KV::AsyncService::RequestGet);
      register_call(*cq, impl, async, &KVImpl::Insert,
                    &KV::AsyncService::RequestInsert);
      register_call(*cq, impl, async, &KVImpl::Update,
                    &KV::AsyncService::RequestUpdate);
      register_call(*cq, impl, async, &KVImpl::Remove,
                    &KV::AsyncService::RequestRemove);
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
    }
  }
}