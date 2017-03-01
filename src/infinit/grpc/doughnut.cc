#include <elle/reactor/scheduler.hh>
#include <infinit/grpc/doughnut.grpc.pb.h>
#include <infinit/grpc/serializer.hh>

# include <elle/athena/paxos/Client.hh>

#include <infinit/model/Model.hh>
#include <infinit/model/MissingBlock.hh>

#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>

ELLE_LOG_COMPONENT("infinit.grpc.doughnut");

namespace infinit
{
  namespace grpc
  {
    inline
    model::Address
    to_address(std::string const& s)
    {
      if (s.empty())
        return model::Address::null;
      if (s.size() == 32)
        return model::Address((const uint8_t*)s.data());
      return model::Address::from_string(s);
    }
    static
    bool
    exception_handler(::DNStatus& res, std::function<void()> f)
    {
      res.set_error(DN_ERROR_OK);
      res.clear_message();
      try
      {
        f();
        ELLE_TRACE("no exception encountered");
        return true;
      }
      catch (model::MissingBlock const& err)
      {
        res.set_error(DN_ERROR_MISSING_BLOCK);
        res.set_message(err.what());
      }
      catch (model::doughnut::ValidationFailed const& err)
      {
        res.set_error(DN_ERROR_VALIDATION_FAILED);
        res.set_message(err.what());
      }
      catch (elle::athena::paxos::TooFewPeers const& err)
      {
        res.set_error(DN_ERROR_TOO_FEW_PEERS);
        res.set_message(err.what());
      }
      catch (model::doughnut::Conflict const& err)
      {
        res.set_error(DN_ERROR_CONFLICT);
        res.set_message(err.what());
        if (auto* mb = dynamic_cast<model::blocks::MutableBlock*>(err.current().get()))
          res.set_version(mb->version());
      }
      catch (elle::Error const& err)
      {
        ELLE_TRACE("generic error handler for %s: %s",
          elle::type_info(err), err);
        ELLE_TRACE("%s", err.backtrace());
        // FIXME
        if (std::string(err.what()).find("no peer available for") == 0)
          res.set_error(DN_ERROR_NO_PEERS);
        else
          res.set_error(DN_ERROR_OTHER);
        res.set_message(err.what());
      }
      ELLE_TRACE("an exception was encountered");
      return false;
    }

    static
    google::protobuf::Message const*
    resolve_anyblock(::AnyBlock const& ab)
    {
      if (ab.has_chb())
        return &ab.chb();
      else if (ab.has_okb())
        return &ab.okb();
      else if (ab.has_acb())
        return &ab.acb();
      else if (ab.has_nb())
        return &ab.nb();
      else
        return nullptr;
    }

    static
    std::string
    anyblock_data(::AnyBlock const& ab)
    {
      if (ab.has_chb())
        return ab.chb().data();
      else if (ab.has_okb())
        return ab.okb().data();
      else if (ab.has_acb())
        return ab.acb().data();
      else if (ab.has_nb())
        return ab.nb().data();
      else
        elle::err("unknown block type!");
      return std::string();
    }

    static
    void
    anyblock_data(::AnyBlock& ab, elle::Buffer const& data)
    {
      if (ab.has_okb())
        ab.mutable_okb()->set_data(data.string());
      else if (ab.has_acb())
        ab.mutable_acb()->set_data(data.string());
    }

    static
    google::protobuf::Message*
    resolve_anyblock(::AnyBlock& ab, model::blocks::Block* b)
    {
      if (dynamic_cast<model::doughnut::NB*>(b))
        return ab.mutable_nb();
      if (dynamic_cast<model::doughnut::CHB*>(b))
        return ab.mutable_chb();
      if (dynamic_cast<model::doughnut::ACB*>(b))
        return ab.mutable_acb();
      if (dynamic_cast<model::doughnut::OKB*>(b))
        return ab.mutable_okb();
      return nullptr;
    }

    class DoughnutImpl: public ::Doughnut::Service
    {
    public:
      using Status = ::grpc::Status;
      using Ctx = ::grpc::ServerContext;
      DoughnutImpl(infinit::model::Model& model)
      : _model(model)
      , _sched(elle::reactor::scheduler())
      {}
      Status MakeCHB(Ctx*, const ::CHBData* request, ::CHB* response);
      Status MakeACB(Ctx*, const ::Empty* request, ::ACB* response);
      Status MakeOKB(Ctx*, const ::Empty* request, ::OKB* response);
      Status MakeNB(Ctx*, const ::String* request, ::NB* response);
      Status Get(Ctx*, const ::DNAddress* request, ::AnyBlockOrStatus* response);
      Status Update(Ctx*, const ::AnyBlock* request, ::DNStatus* response);
      Status Insert(Ctx*, const ::AnyBlock* request, ::DNStatus* response);
      Status Remove(Ctx*, const ::DNAddress* request, ::DNStatus* response);
    private:
      infinit::model::Model& _model;
      elle::reactor::Scheduler& _sched;
    };

    ::grpc::Status DoughnutImpl::MakeCHB(Ctx*, const ::CHBData* request, ::CHB* response)
    {
      ::DNStatus status;
      _sched.mt_run<void>("MakeCHB", [&] {
          if (!exception_handler(status, [&] {
              std::unique_ptr<model::blocks::Block> block
                = _model.make_block<model::blocks::ImmutableBlock>(
                    elle::Buffer(request->data()),
                    to_address(request->owner()));
              SerializerOut sout(response);
              sout.serialize_forward(block);
          }))
          {
            ELLE_ERR("exception: %s", status.message());
          }
      });
      return ::grpc::Status::OK;
    }
    ::grpc::Status DoughnutImpl::MakeACB(Ctx*, const ::Empty* request, ::ACB* response)
    {
      ::DNStatus status;
      _sched.mt_run<void>("MakeACB", [&] {
          if (!exception_handler(status, [&] {
              std::unique_ptr<model::blocks::Block> block = _model.make_block<model::blocks::ACLBlock>();
              SerializerOut sout(response);
              sout.serialize_forward(block);
          }))
          {
            ELLE_ERR("exception: %s", status.message());
          }
      });
      return ::grpc::Status::OK;
    }
    ::grpc::Status DoughnutImpl::MakeOKB(Ctx*, const ::Empty* request, ::OKB* response)
    {
      ::DNStatus status;

      _sched.mt_run<void>("MakeOKB", [&] {
          if (!exception_handler(status, [&] {
              std::unique_ptr<model::blocks::Block> block = _model.make_block<model::blocks::MutableBlock>();
              SerializerOut sout(response);
              sout.serialize_forward(block);
          }))
          {
            ELLE_ERR("exception: %s", status.message());
          }
      });
      return ::grpc::Status::OK;
    }
    ::grpc::Status DoughnutImpl::MakeNB(Ctx*, const ::String* request, ::NB* response)
    {
      _sched.mt_run<void>("MakeNB", [&] {
          std::unique_ptr<model::blocks::Block> nb = std::make_unique<model::doughnut::NB>(
            dynamic_cast<model::doughnut::Doughnut&>(_model),
            request->str(),
            elle::Buffer());
          SerializerOut sout(response);
          sout.serialize_forward(nb);
      });
      return ::grpc::Status::OK;
    }
    ::grpc::Status DoughnutImpl::Get(Ctx*, const ::DNAddress* request, ::AnyBlockOrStatus* response)
    {
      ::DNStatus status;
      _sched.mt_run<void>("Get", [&] {
          if (!exception_handler(status, [&] {
              auto block = _model.fetch(to_address(request->address()));
              SerializerOut sout(resolve_anyblock(*response->mutable_block(), block.get()));
              sout.serialize_forward(block);
              anyblock_data(*response->mutable_block(), block->data());
          }))
          {
            response->mutable_status()->CopyFrom(status);
          }
      });
      return ::grpc::Status::OK;
    }
    ::grpc::Status DoughnutImpl::Update(Ctx*, const ::AnyBlock* request, ::DNStatus* response)
    {
      _sched.mt_run<void>("Update", [&] {
          exception_handler(*response, [&] {
              SerializerIn sin(resolve_anyblock(*request));
              sin.set_context<model::doughnut::Doughnut*>(
                dynamic_cast<model::doughnut::Doughnut*>(&_model));
              std::unique_ptr<model::blocks::Block> block;
              sin.serialize_forward(block);
              // force a seal
              if (auto mb = dynamic_cast<model::blocks::MutableBlock*>(block.get()))
              {
                mb->data(anyblock_data(*request));
              }
              _model.update(std::move(block));
          });
      });
      return ::grpc::Status::OK;
    }
    ::grpc::Status DoughnutImpl::Insert(Ctx*, const ::AnyBlock* request, ::DNStatus* response)
    {
      _sched.mt_run<void>("Insert", [&] {
          exception_handler(*response, [&] {
              SerializerIn sin(resolve_anyblock(*request));
              sin.set_context<model::doughnut::Doughnut*>(
                dynamic_cast<model::doughnut::Doughnut*>(&_model));
              std::unique_ptr<model::blocks::Block> block;
              sin.serialize_forward(block);
              // force a seal
              if (auto mb = dynamic_cast<model::blocks::MutableBlock*>(block.get()))
              {
                mb->data(anyblock_data(*request));
              }
              _model.insert(std::move(block));
          });
      });
      return ::grpc::Status::OK;
    }
    ::grpc::Status DoughnutImpl::Remove(Ctx*, const ::DNAddress* request, ::DNStatus* response)
    {
      _sched.mt_run<void>("Remove", [&] {
          exception_handler(*response, [&] {
              _model.remove(to_address(request->address()));
          });
      });
      return ::grpc::Status::OK;
    }

    std::unique_ptr< ::grpc::Service>
    doughnut_service(model::Model& dht)
    {
      return std::make_unique<DoughnutImpl>(dht);
    }
  }
}