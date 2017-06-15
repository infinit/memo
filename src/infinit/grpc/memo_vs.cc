#include <boost/function_types/function_type.hpp>

#include <elle/reactor/scheduler.hh>
#include <elle/serialization/json.hh>

#include <infinit/grpc/memo_vs.grpc.pb.h>
#include <infinit/grpc/grpc.hh>
#include <infinit/grpc/serializer.hh>

# include <elle/athena/paxos/Client.hh>

#include <infinit/model/Model.hh>
#include <infinit/model/Conflict.hh>
#include <infinit/model/MissingBlock.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>

ELLE_LOG_COMPONENT("infinit.grpc.doughnut");


namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<std::unique_ptr<infinit::model::blocks::MutableBlock >>
    {
      static
      void
      serialize(std::unique_ptr<infinit::model::blocks::MutableBlock> const& b,
                SerializerOut&s)
      {
        auto* ptr = static_cast<infinit::model::blocks::Block*>(b.get());
        s.serialize_forward(ptr);
      }

      static
      std::unique_ptr<infinit::model::blocks::MutableBlock>
      deserialize(SerializerIn& s)
      {
        std::unique_ptr<infinit::model::blocks::Block> bl;
        s.serialize_forward(bl);
        return std::unique_ptr<infinit::model::blocks::MutableBlock>(
          dynamic_cast<infinit::model::blocks::MutableBlock*>(bl.release()));
      }
    };

    template<>
    struct Serialize<std::unique_ptr<infinit::model::blocks::ImmutableBlock>>
    {
      static
      void
      serialize(std::unique_ptr<infinit::model::blocks::ImmutableBlock> const& b,
                SerializerOut&s)
      {
        auto* ptr = static_cast<infinit::model::blocks::Block*>(b.get());
        s.serialize_forward(ptr);
      }

      static
      std::unique_ptr<infinit::model::blocks::ImmutableBlock>
      deserialize(SerializerIn& s)
      {
        std::unique_ptr<infinit::model::blocks::Block> bl;
        s.serialize_forward(bl);
        return std::unique_ptr<infinit::model::blocks::ImmutableBlock>(
          dynamic_cast<infinit::model::blocks::ImmutableBlock*>(bl.release()));
      }
    };
  }
}

namespace infinit
{
  namespace grpc
  {
    class Service;
    template <typename NF, typename REQ, typename RESP, bool NOEXCEPT>
    ::grpc::Status
    invoke_named(Service& service,
                 elle::reactor::Scheduler& sched,
                 model::doughnut::Doughnut& dht,
                 NF& nf,
                 ::grpc::ServerContext* ctx,
                 const REQ* request,
                 RESP* response);

    class Service: public ::grpc::Service
    {
    public:
      Service()
      {
#if INFINIT_ENABLE_PROMETHEUS
        _family = infinit::prometheus::instance().make_counter_family(
          "infinit_grpc_calls",
          "How many grpc calls are made");
      auto errf = infinit::prometheus::instance().make_counter_family(
          "infinit_grpc_result",
          "Result type of grpc call");
      auto& inst = infinit::prometheus::instance();
      errOk = inst.make(errf, {{"result", "ok"}});
      errPermission = inst.make(errf, {{"result", "permission_denied"}});
      errTooFewPeers = inst.make(errf, {{"result", "too_few_peers"}});
      errConflict = inst.make(errf, {{"result", "conflict"}});
      errOther = inst.make(errf, {{"result", "other_failure"}});
      errMissingMutable = inst.make(errf, {{"result", "missing_mutable"}});
      errMissingImmutable = inst.make(errf, {{"result", "missing_immutable"}});
#endif
      }

      template <typename GArg, typename GRet, bool NOEXCEPT=false, typename NF>
      void AddMethod(NF& nf, model::doughnut::Doughnut& dht,
                     std::string const& route)
      {
        auto& sched = elle::reactor::scheduler();
        _method_names.push_back(std::make_unique<std::string>(route));
        int index = 0;
#if INFINIT_ENABLE_PROMETHEUS
        _counters.push_back(infinit::prometheus::instance()
          .make(_family,
            {{"call", route}}));
        index = _counters.size()-1;
#endif

        ::grpc::Service::AddMethod(new ::grpc::RpcServiceMethod(
          _method_names.back()->c_str(),
          ::grpc::RpcMethod::NORMAL_RPC,
          new ::grpc::RpcMethodHandler<Service, GArg, GRet>(
            [&, index, this](Service*,
                       ::grpc::ServerContext* ctx, const GArg* arg, GRet* ret)
            {
#if INFINIT_ENABLE_PROMETHEUS
              if (auto& c = _counters[index])
                c->Increment();
#endif
              return invoke_named<NF, GArg, GRet, NOEXCEPT>(*this, sched, dht,
                nf, ctx, arg, ret);
            },
            this)));
      }

    private:
#if INFINIT_ENABLE_PROMETHEUS
      prometheus::Family<prometheus::Counter>* _family;
      std::vector<prometheus::CounterPtr> _counters;
    public:
      prometheus::CounterPtr errOk;
      prometheus::CounterPtr errPermission;
      prometheus::CounterPtr errTooFewPeers;
      prometheus::CounterPtr errConflict;
      prometheus::CounterPtr errOther;
      prometheus::CounterPtr errMissingMutable;
      prometheus::CounterPtr errMissingImmutable;
#endif
    private:
      std::vector<std::unique_ptr<std::string>> _method_names;
    };

    template <typename T>
    struct OptionFirst
    {};

    template <typename A, typename E>
    struct OptionFirst<elle::Option<A, E>>
    {
      template <typename T>
      static
      A*
      value(T& v, ::grpc::Status& err)
      {
        if (v.template is<E>())
        {
          elle::err("unexpected exception: %s", v.template get<E>());
          err = ::grpc::Status(::grpc::INTERNAL,
                               elle::exception_string(v.template get<E>()));
          return nullptr;
        }
        return &v.template get<A>();
      }
    };

    template<typename T>
    struct ExceptionExtracter
    {};

    template <typename A, typename E>
    struct ExceptionExtracter<elle::Option<A, E>>
    {
      template <typename T>
      static
      void
      value(Service& service, SerializerOut& sout, T& v,
            ::grpc::Status& err,
            elle::Version const& version,
            bool is_void)
      {
         if (v.template is<E>())
         {
           ELLE_DEBUG("grpc call failed with exception %s",
                      elle::exception_string(v.template get<E>()));
           try
           {
             std::rethrow_exception(v.template get<E>());
           }
           catch (infinit::model::MissingBlock const& mb)
           {
             err = ::grpc::Status(::grpc::NOT_FOUND, mb.what());
#if INFINIT_ENABLE_PROMETHEUS
             if (mb.address().mutable_block() && service.errMissingMutable)
               service.errMissingMutable->Increment();
             if (!mb.address().mutable_block() && service.errMissingImmutable)
               service.errMissingImmutable->Increment();
#endif
           }
           catch (infinit::model::doughnut::ValidationFailed const& vf)
           {
#if INFINIT_ENABLE_PROMETHEUS
             if (service.errPermission)
               service.errPermission->Increment();
#endif
             err = ::grpc::Status(::grpc::PERMISSION_DENIED, vf.what());
           }
           catch (elle::athena::paxos::TooFewPeers const& tfp)
           {
#if INFINIT_ENABLE_PROMETHEUS
             if (service.errTooFewPeers)
               service.errTooFewPeers->Increment();
#endif
             err = ::grpc::Status(::grpc::UNAVAILABLE, tfp.what());
           }
           catch (infinit::model::Conflict const& c)
           {
#if INFINIT_ENABLE_PROMETHEUS
             if (service.errConflict)
               service.errConflict->Increment();
#endif
             elle::unconst(c).serialize(sout, version);
           }
           catch (elle::Error const& e)
           {
#if INFINIT_ENABLE_PROMETHEUS
             if (service.errOther)
               service.errOther->Increment();
#endif
             err = ::grpc::Status(::grpc::INTERNAL, e.what());
           }
         }
         else
         {
#if INFINIT_ENABLE_PROMETHEUS
           if (service.errOk)
             service.errOk->Increment();
#endif
           if (!is_void)
             sout.serialize(
               cxx_to_message_name(elle::type_info<A>().name()),
               v.template get<A>());
         }
      }
    };

    template <typename NF, typename REQ, typename RESP, bool NOEXCEPT>
    ::grpc::Status
    invoke_named(Service& service,
                 elle::reactor::Scheduler& sched,
                 model::doughnut::Doughnut& dht,
                 NF& nf,
                 ::grpc::ServerContext* ctx,
                 const REQ* request,
                 RESP* response)
    {
      auto status = ::grpc::Status::OK;
      auto code = ::grpc::INTERNAL;
      Task task;
      if (!task.proceed())
        return ::grpc::Status(::grpc::INTERNAL, "server is shuting down");
      sched.mt_run<void>(
        elle::print("invoke %r", elle::type_info<REQ>().name()),
        [&] {
          auto& thread = *ELLE_ENFORCE(elle::reactor::scheduler().current());
          thread.name(elle::print(
                        "{} ({})", thread.name(), static_cast<void*>(&thread)));
          try
          {
            ELLE_TRACE("invoking some method: %s -> %s",
                       elle::type_info<REQ>().name(),
                       elle::type_info<RESP>().name());
            SerializerIn sin(request);
            sin.set_context<model::doughnut::Doughnut*>(&dht);
            code = ::grpc::INVALID_ARGUMENT;
            auto call = typename NF::Call(sin);
            code = ::grpc::INTERNAL;
            auto res = nf(std::move(call));
            ELLE_DUMP("adapter with %s",
                      elle::type_info<typename NF::Result>());
            SerializerOut sout(response);
            sout.set_context<model::doughnut::Doughnut*>(&dht);
            if (NOEXCEPT) // It will compile anyway no need for static switch
            {
              auto* adapted =
                OptionFirst<typename NF::Result::Super>::value(res, status);
              if (status.ok() && !decltype(res)::is_void::value)
                sout.serialize_forward(*adapted);
            }
            else
            {
              ExceptionExtracter<typename NF::Result::Super>::value(
                service, sout, res, status, dht.version(),
                decltype(res)::is_void::value);
            }
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE("GRPC invoke failed with %s", e);
            status = ::grpc::Status(code, e.what());
          }
      });
      return status;
    }

    using Update =
      std::function<void(std::unique_ptr<infinit::model::blocks::Block>,
         std::unique_ptr<infinit::model::ConflictResolver>, bool)>;

    Update
    make_update_wrapper(Update f)
    {
      // model's update() function does not handle parallel calls with the same
      // address, so wrap our call with a per-address mutex
      using MutexPtr = std::shared_ptr<elle::reactor::Mutex>;
      using MutexWeak = std::weak_ptr<elle::reactor::Mutex>;
      using MutexMap = std::unordered_map<infinit::model::Address, MutexWeak>;
      auto map = std::make_shared<MutexMap>();
      return [map, f] (
        std::unique_ptr<infinit::model::blocks::Block> block,
        std::unique_ptr<infinit::model::ConflictResolver> resolver,
        bool decrypt_data)
      {
        auto addr = block->address();
        auto it = map->find(addr);
        std::shared_ptr<elle::reactor::Mutex> mutex;
        if (it == map->end())
        {
          mutex = MutexPtr(new elle::reactor::Mutex(),
            [map_ptr=map.get(), addr](elle::reactor::Mutex* m)
                           {
                             map_ptr->erase(addr); delete m;
                           });
          (*map)[addr] = mutex;
        }
        else
          mutex = it->second.lock();
        elle::reactor::Lock lock(*mutex);
        f(std::move(block), std::move(resolver), decrypt_data);
      };
    }

    std::unique_ptr<::grpc::Service>
    doughnut_service(model::Model& model)
    {
      auto& dht = dynamic_cast<model::doughnut::Doughnut&>(model);
      using UpdateNamed = decltype(dht.update);
      // We need to store our wrapper somewhere
      static std::vector<std::shared_ptr<UpdateNamed>> update_wrappers;
      auto ptr = std::make_unique<Service>();
      ptr->AddMethod<::memo::vs::FetchRequest, ::memo::vs::FetchResponse>
        (dht.fetch, dht, "/memo.vs.ValueStore/Fetch");
      ptr->AddMethod<::memo::vs::InsertRequest, ::memo::vs::InsertResponse>
        (dht.insert, dht, "/memo.vs.ValueStore/Insert");
      Update update = dht.update.function();
      update_wrappers.emplace_back(new UpdateNamed(
        make_update_wrapper(dht.update.function()),
        infinit::model::block,
        infinit::model::conflict_resolver = nullptr,
        infinit::model::decrypt_data = false));
      ptr->AddMethod<::memo::vs::UpdateRequest, ::memo::vs::UpdateResponse>
        (*update_wrappers.back(), dht, "/memo.vs.ValueStore/Update");
      ptr->AddMethod<::memo::vs::MakeMutableBlockRequest, ::memo::vs::Block, true>
        (dht.make_mutable_block, dht, "/memo.vs.ValueStore/MakeMutableBlock");
      ptr->AddMethod<::memo::vs::MakeImmutableBlockRequest, ::memo::vs::Block, true>
        (dht.make_immutable_block, dht,"/memo.vs.ValueStore/MakeImmutableBlock");
      ptr->AddMethod<::memo::vs::DeleteRequest, ::memo::vs::DeleteResponse>
        (dht.remove, dht, "/memo.vs.ValueStore/Delete");
      ptr->AddMethod<::memo::vs::MakeNamedBlockRequest, ::memo::vs::Block, true>
        (dht.make_named_block, dht, "/memo.vs.ValueStore/MakeNamedBlock");
      ptr->AddMethod<::memo::vs::NamedBlockAddressRequest,
        ::memo::vs::NamedBlockAddressResponse, true>
        (dht.named_block_address, dht, "/memo.vs.ValueStore/NamedBlockAddress");
      return std::move(ptr);
    }
  }
}
