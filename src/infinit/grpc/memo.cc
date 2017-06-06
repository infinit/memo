#include <boost/function_types/function_type.hpp>

#include <elle/reactor/scheduler.hh>
#include <elle/serialization/json.hh>

#include <infinit/grpc/memo.grpc.pb.h>
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
      value(SerializerOut& sout, T& v, ::grpc::Status& err,
            elle::Version const& version,
            bool is_void)
      {
        ELLE_DEBUG("grpc call failed with exception %s",
                   elle::exception_string(v.template get<E>()));
        if (v.template is<E>())
        {
          try
          {
            std::rethrow_exception(v.template get<E>());
          }
          catch (infinit::model::MissingBlock const& mb)
          {
            err = ::grpc::Status(::grpc::NOT_FOUND, mb.what());
          }
          catch (infinit::model::doughnut::ValidationFailed const& vf)
          {
            err = ::grpc::Status(::grpc::PERMISSION_DENIED, vf.what());
          }
          catch (elle::athena::paxos::TooFewPeers const& tfp)
          {
            err = ::grpc::Status(::grpc::UNAVAILABLE, tfp.what());
          }
          catch (infinit::model::Conflict const& c)
          {
            elle::unconst(c).serialize(sout, version);
          }
          catch (elle::Error const& e)
          {
            err = ::grpc::Status(::grpc::INTERNAL, e.what());
          }
        }
        else
        {
          if (!is_void)
            sout.serialize(
              cxx_to_message_name(elle::type_info<A>().name()),
              v.template get<A>());
        }
      }
    };

    template <typename NF, typename REQ, typename RESP, bool NOEXCEPT>
    ::grpc::Status
    invoke_named(elle::reactor::Scheduler& sched,
                 model::doughnut::Doughnut& dht,
                 NF& nf,
                 ::grpc::ServerContext* ctx,
                 const REQ* request,
                 RESP* response)
    {
      ::grpc::Status status = ::grpc::Status::OK;
      ::grpc::StatusCode code = ::grpc::INTERNAL;
      sched.mt_run<void>("invoke", [&] {
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
            if (NOEXCEPT) // it will compile anyway no need for static switch
            {
              auto* adapted = OptionFirst<typename NF::Result::Super>::value(
                res, status);
              if (status.ok() && !decltype(res)::is_void::value)
                sout.serialize_forward(*adapted);
            }
            else
            {
              ExceptionExtracter<typename NF::Result::Super>::value(
                sout, res, status, dht.version(),
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

    class Service
      : public ::grpc::Service
    {
    public:
      /// Register a Remote Procedure Call.
      ///
      /// @tparam GArg The RPC argument type.
      /// @tparam GRet The RPC return type.
      /// @tparam NOEXCEPT Whether a failure will cause the method not to raise
      ///                  an exception.
      /// @tparam NF The type of the method bound.
      /// @param nf The method to bind.
      /// @param dht The Doughnut instance to call the bound method to.
      /// @param name The route of the RPC.
      ///
      /// Note. The route can be found in the generated the .bp file.
      ///       The format is (<package>.)<service>/<method>.
      template <typename GArg, typename GRet, bool NOEXCEPT=false, typename NF>
      void
      AddMethod(NF& nf, model::doughnut::Doughnut& dht,
                std::string const& route)
      {
        auto& sched = elle::reactor::scheduler();
        this->_method_names.push_back(std::make_unique<std::string>(route));
        ::grpc::Service::AddMethod(new ::grpc::RpcServiceMethod(
          this->_method_names.back()->c_str(),
          ::grpc::RpcMethod::NORMAL_RPC,
          new ::grpc::RpcMethodHandler<Service, GArg, GRet>(
            [&](Service*,
                ::grpc::ServerContext* ctx, const GArg* arg, GRet* ret)
            {
              return invoke_named<NF, GArg, GRet, NOEXCEPT>(
                sched, dht, nf, ctx, arg, ret);
            },
            this)));
      }
    private:
      std::vector<std::unique_ptr<std::string>> _method_names;
    };

    using Update = std::function<void (
      std::unique_ptr<infinit::model::blocks::Block>,
      std::unique_ptr<infinit::model::ConflictResolver>,
      bool)>;

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
      ptr->AddMethod<::memo::FetchRequest, ::memo::FetchResponse>
        (dht.fetch, dht, "/memo.ValueStore/Fetch");
      ptr->AddMethod<::memo::InsertRequest, ::memo::InsertResponse>
        (dht.insert, dht, "/memo.ValueStore/Insert");
      Update update = dht.update.function();
      update_wrappers.emplace_back(new UpdateNamed(
        make_update_wrapper(dht.update.function()),
        infinit::model::block,
        infinit::model::conflict_resolver = nullptr,
        infinit::model::decrypt_data = false));
      ptr->AddMethod<::memo::UpdateRequest, ::memo::UpdateResponse>
        (*update_wrappers.back(), dht, "/memo.ValueStore/Update");
      ptr->AddMethod<::memo::MakeMutableBlockRequest, ::memo::Block, true>
        (dht.make_mutable_block, dht, "/memo.ValueStore/MakeMutableBlock");
      ptr->AddMethod<::memo::MakeImmutableBlockRequest, ::memo::Block, true>
        (dht.make_immutable_block, dht,"/memo.ValueStore/MakeImmutableBlock");
      ptr->AddMethod<::memo::MakeNamedBlockRequest, ::memo::Block, true>
        (dht.make_named_block, dht, "/memo.ValueStore/MakeNamedBlock");
      ptr->AddMethod<::memo::NamedBlockAddressRequest,
                     ::memo::NamedBlockAddressResponse , true>
        (dht.named_block_address, dht, "/memo.ValueStore/NamedBlockAddress");
      ptr->AddMethod<::memo::RemoveRequest, ::memo::RemoveResponse>
        (dht.remove, dht, "/memo.ValueStore/Remove");
      return std::move(ptr);
    }
  }
}
