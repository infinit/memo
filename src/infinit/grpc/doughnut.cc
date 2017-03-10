#include <boost/function_types/function_type.hpp>

#include <elle/reactor/scheduler.hh>
#include <elle/serialization/json.hh>

#include <infinit/grpc/doughnut.grpc.pb.h>
#include <infinit/grpc/serializer.hh>

# include <elle/athena/paxos/Client.hh>

#include <infinit/model/Model.hh>
#include <infinit/model/MissingBlock.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>

ELLE_LOG_COMPONENT("infinit.grpc.doughnut");


namespace elle
{
  namespace serialization
  {
    template<> struct Serialize<std::unique_ptr<infinit::model::blocks::MutableBlock >>
    {
      static void serialize(
        std::unique_ptr<infinit::model::blocks::MutableBlock> const& b,
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
    template<> struct Serialize<std::unique_ptr<infinit::model::blocks::ImmutableBlock>>
    {
      static void serialize(
        std::unique_ptr<infinit::model::blocks::ImmutableBlock> const& b,
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

    template<typename RT, typename SUBT>
    struct Adapter
    {
      template<typename T2>
      static
      RT adapt(T2 v)
      {
        return std::move(v);
      }
    };
    template<typename SB, typename EO2>
    struct AdapterHelper
    {
      using type = elle::Option<SB,EO2>;
      using target = elle::Option<std::unique_ptr<infinit::model::blocks::Block>, EO2>;
      template<typename T>
      static target adapt(T v)
      {
        if (v.template is<EO2>())
          return target(v.template get<EO2>());
        else
          return target(std::unique_ptr<infinit::model::blocks::Block>(
            v.template get<SB>().release()));
      }
    };
    template<typename RT, typename EO2>
    struct Adapter<RT, elle::Option<std::unique_ptr<infinit::model::blocks::ImmutableBlock>, EO2>>
    : public AdapterHelper<std::unique_ptr<infinit::model::blocks::ImmutableBlock>, EO2>
    {};
    template<typename RT, typename EO2>
    struct Adapter<RT, elle::Option<std::unique_ptr<infinit::model::blocks::MutableBlock>, EO2>>
    : public AdapterHelper<std::unique_ptr<infinit::model::blocks::MutableBlock>, EO2>
    {};
    template<typename NF, typename REQ, typename RESP>
    ::grpc::Status
    invoke_named(elle::reactor::Scheduler& sched, model::doughnut::Doughnut& dht, NF& nf, ::grpc::ServerContext* ctx, const REQ* request, RESP* response)
    {
      sched.mt_run<void>("invoke", [&] {
          try
          {
            ELLE_TRACE("invoking some method: %s -> %s", elle::type_info<REQ>().name(), elle::type_info<RESP>().name());
            SerializerIn sin(request);
            sin.set_context<model::doughnut::Doughnut*>(&dht);
            typename NF::Call call(sin);
            typename NF::Result res = nf(std::move(call));
            ELLE_DUMP("adapter with %s", elle::type_info<typename NF::Result>());
            SerializerOut sout(response);
            sout.set_context<model::doughnut::Doughnut*>(&dht);
            auto adapted = Adapter<typename NF::Result, typename NF::Result::Super>::adapt(std::move(res));
            sout.serialize_forward(adapted);
          }
          catch (elle::Error const& e)
          {
            ELLE_ERR("boum %s", e);
            ELLE_ERR("%s", e.backtrace());
          }
      });
      return ::grpc::Status::OK;
    }

    class Service: public ::grpc::Service
    {
    public:
      template<typename GArg, typename GRet, typename NF>
      void AddMethod(NF& nf, model::doughnut::Doughnut& dht, std::string const& name)
      {
        auto& sched = elle::reactor::scheduler();
        using sig = std::function<::grpc::Status(Service*,::grpc::ServerContext*, const GArg *, GRet*)>;
        ::grpc::Service::AddMethod(new ::grpc::RpcServiceMethod(
          (new std::string(name))->c_str(), ::grpc::RpcMethod::NORMAL_RPC,
          new ::grpc::RpcMethodHandler<Service, GArg, GRet>(
            (sig)std::bind(
              &invoke_named<NF, GArg, GRet>,
              std::ref(sched), std::ref(dht), std::ref(nf),
              std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
            this)));
      }
    };

    std::unique_ptr< ::grpc::Service>
    doughnut_service(model::Model& model)
    {
      auto& dht = dynamic_cast<model::doughnut::Doughnut&>(model);
      auto ptr = std::make_unique<Service>();
      ptr->AddMethod<::Fetch, ::BlockOrException>(dht.fetch, dht, "/Doughnut/fetch");
      ptr->AddMethod<::Insert, ::EmptyOrException>(dht.insert, dht, "/Doughnut/insert");
      ptr->AddMethod<::Update, ::EmptyOrException>(dht.update, dht, "/Doughnut/update");
      ptr->AddMethod<::Empty, ::BlockOrException>(dht.make_mutable_block, dht, "/Doughnut/make_mutable_block");
      ptr->AddMethod<::CHBData, ::BlockOrException>(dht.make_immutable_block, dht,"/Doughnut/make_immutable_block");
      ptr->AddMethod<::NamedBlockKey, ::BlockOrException>(dht.make_named_block, dht, "/Doughnut/make_named_block");
      ptr->AddMethod<::NamedBlockKey, ::AddressOrException>(dht.named_block_address, dht, "/Doughnut/named_block_address");
      return ptr;
    }
  }
}