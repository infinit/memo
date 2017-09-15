#include <boost/function_types/function_type.hpp>

#include <elle/reactor/scheduler.hh>
#include <elle/serialization/json.hh>

#include <memo/grpc/memo_vs_with_named.grpc.pb.h>
#include <memo/grpc/grpc.hh>
#include <memo/grpc/serializer.hh>

# include <elle/athena/paxos/Client.hh>

#include <memo/model/Model.hh>
#include <memo/model/Conflict.hh>
#include <memo/model/MissingBlock.hh>

#include <memo/model/doughnut/Doughnut.hh>
#include <memo/model/doughnut/ACB.hh>
#include <memo/model/doughnut/CHB.hh>
#include <memo/model/doughnut/NB.hh>
#include <memo/model/doughnut/User.hh>
#include <memo/model/doughnut/ValidationFailed.hh>

ELLE_LOG_COMPONENT("memo.grpc.doughnut");

namespace prom = memo::prometheus;

namespace elle
{
  namespace serialization
  {
    template<>
    struct Serialize<std::unique_ptr<memo::model::blocks::MutableBlock >>
    {
      static
      void
      serialize(std::unique_ptr<memo::model::blocks::MutableBlock> const& b,
                SerializerOut&s)
      {
        auto* ptr = static_cast<memo::model::blocks::Block*>(b.get());
        s.serialize_forward(ptr);
      }

      static
      std::unique_ptr<memo::model::blocks::MutableBlock>
      deserialize(SerializerIn& s)
      {
        std::unique_ptr<memo::model::blocks::Block> bl;
        s.serialize_forward(bl);
        return std::unique_ptr<memo::model::blocks::MutableBlock>(
          dynamic_cast<memo::model::blocks::MutableBlock*>(bl.release()));
      }
    };

    template<>
    struct Serialize<std::unique_ptr<memo::model::blocks::ImmutableBlock>>
    {
      static
      void
      serialize(std::unique_ptr<memo::model::blocks::ImmutableBlock> const& b,
                SerializerOut&s)
      {
        auto* ptr = static_cast<memo::model::blocks::Block*>(b.get());
        s.serialize_forward(ptr);
      }

      static
      std::unique_ptr<memo::model::blocks::ImmutableBlock>
      deserialize(SerializerIn& s)
      {
        std::unique_ptr<memo::model::blocks::Block> bl;
        s.serialize_forward(bl);
        return std::unique_ptr<memo::model::blocks::ImmutableBlock>(
          dynamic_cast<memo::model::blocks::ImmutableBlock*>(bl.release()));
      }
    };
  }
}

namespace memo
{
  namespace grpc
  {
    class Service;

    template <typename NF, typename REQ, typename RESP, bool NoExcept>
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
      template <typename GArg, typename GRet, bool NoExcept=false, typename NF>
      void AddMethod(NF& nf, model::doughnut::Doughnut& dht,
                     std::string const& route)
      {
        auto& sched = elle::reactor::scheduler();
        this->_method_names.push_back(std::make_unique<std::string>(route));
        this->_counters.push_back(prom::make(_call_f, {{"call", route}}));
        auto const index = this->_counters.size()-1;

        ::grpc::Service::AddMethod(
          new ::grpc::RpcServiceMethod(
            this->_method_names.back()->c_str(),
            ::grpc::RpcMethod::NORMAL_RPC,
            new ::grpc::RpcMethodHandler<Service, GArg, GRet>(
              [&, index, this](
                Service*, ::grpc::ServerContext* ctx, const GArg* arg,
                GRet* ret)
              {
                increment(_counters[index]);
                return invoke_named<NF, GArg, GRet, NoExcept>(
                  *this, sched, dht, nf, ctx, arg, ret);
              },
              this)));
      }

    private:
      /// Counter family for method calls.
      prom::Family<prom::Counter>* _call_f
        = prom::make_counter_family("memo_grpc_calls",
                                    "How many grpc calls are made");
      /// Call counters for methods.  One per method.
      std::vector<prom::CounterPtr> _counters;

      /// Counter family for method results.
      prom::Family<prom::Counter>* _res_f
        = prom::make_counter_family("memo_grpc_result",
                                    "Result type of grpc call");
    public:
      prom::CounterPtr errOk = prom::make(_res_f, {{"result", "ok"}});
      prom::CounterPtr errPermission = prom::make(_res_f, {{"result", "permission_denied"}});
      prom::CounterPtr errTooFewPeers = prom::make(_res_f, {{"result", "too_few_peers"}});
      prom::CounterPtr errConflict = prom::make(_res_f, {{"result", "conflict"}});
      prom::CounterPtr errOther = prom::make(_res_f, {{"result", "other_failure"}});
      prom::CounterPtr errMissingMutable = prom::make(_res_f, {{"result", "missing_mutable"}});
      prom::CounterPtr errMissingImmutable = prom::make(_res_f, {{"result", "missing_immutable"}});

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
          catch (memo::model::MissingBlock const& mb)
          {
            err = ::grpc::Status(::grpc::NOT_FOUND, mb.what());
            if (mb.address().mutable_block())
              increment(service.errMissingMutable);
            if (!mb.address().mutable_block())
              increment(service.errMissingImmutable);
          }
          catch (memo::model::doughnut::ValidationFailed const& vf)
          {
            increment(service.errPermission);
            err = ::grpc::Status(::grpc::PERMISSION_DENIED, vf.what());
          }
          catch (elle::athena::paxos::TooFewPeers const& tfp)
          {
            increment(service.errTooFewPeers);
            err = ::grpc::Status(::grpc::UNAVAILABLE, tfp.what());
          }
          catch (memo::model::Conflict const& c)
          {
            increment(service.errConflict);
            elle::unconst(c).serialize(sout, version);
          }
          catch (elle::Error const& e)
          {
            increment(service.errOther);
            err = ::grpc::Status(::grpc::INTERNAL, e.what());
          }
        }
        else
        {
          increment(service.errOk);
          if (!is_void)
            sout.serialize(
              cxx_to_message_name(elle::type_info<A>().name()),
              v.template get<A>());
        }
      }
    };

    template <typename NF, typename REQ, typename RESP, bool NoExcept>
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
            if (NoExcept) // It will compile anyway no need for static switch
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
      std::function<auto (std::unique_ptr<memo::model::blocks::Block>,
                          std::unique_ptr<memo::model::ConflictResolver>,
                          bool)
                    -> void>;

    Update
    make_update_wrapper(Update f)
    {
      // model's update() function does not handle parallel calls with the same
      // address, so wrap our call with a per-address mutex
      using MutexPtr = std::shared_ptr<elle::reactor::Mutex>;
      using MutexWeak = std::weak_ptr<elle::reactor::Mutex>;
      using MutexMap = std::unordered_map<memo::model::Address, MutexWeak>;
      auto map = std::make_shared<MutexMap>();
      return [map, f] (
        std::unique_ptr<memo::model::blocks::Block> block,
        std::unique_ptr<memo::model::ConflictResolver> resolver,
        bool decrypt_data)
      {
        auto const addr = block->address();
        auto mutex = MutexPtr{};
        auto it = map->find(addr);
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
        memo::model::block,
        memo::model::conflict_resolver = nullptr,
        memo::model::decrypt_data = false));
      ptr->AddMethod<::memo::vs::UpdateRequest, ::memo::vs::UpdateResponse>
        (*update_wrappers.back(), dht, "/memo.vs.ValueStore/Update");
      ptr->AddMethod<::memo::vs::MakeMutableBlockRequest, ::memo::vs::Block, true>
        (dht.make_mutable_block, dht, "/memo.vs.ValueStore/MakeMutableBlock");
      ptr->AddMethod<::memo::vs::MakeImmutableBlockRequest, ::memo::vs::Block, true>
        (dht.make_immutable_block, dht,"/memo.vs.ValueStore/MakeImmutableBlock");
      ptr->AddMethod<::memo::vs::InsertImmutableBlockRequest, ::memo::vs::InsertImmutableBlockResponse, true>
        (dht.insert_immutable_block, dht,"/memo.vs.ValueStore/InsertImmutableBlock");
      ptr->AddMethod<::memo::vs::InsertMutableBlockRequest, ::memo::vs::InsertMutableBlockResponse, true>
        (dht.insert_mutable_block, dht,"/memo.vs.ValueStore/InsertMutableBlock");
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
