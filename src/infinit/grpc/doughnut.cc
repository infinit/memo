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
    exception_handler(::Status& res, std::function<void()> f)
    {
      res.set_error(ERROR_OK);
      res.clear_message();
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
        ELLE_TRACE("%s", err.backtrace());
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

    class DoughnutImpl: public ::Doughnut::Service
    {
    public:
      using Status = ::grpc::Status;
      using Ctx = ::grpc::ServerContext;
      DoughnutImpl(infinit::model::Model& model)
      : _model(model)
      , _sched(elle::reactor::scheduler())
      {}
      Status MakeCHB(Ctx*, const ::CHBData* request, ::Block* response);
      Status MakeACB(Ctx*, const ::Empty* request, ::Block* response);
      Status MakeOKB(Ctx*, const ::Empty* request, ::Block* response);
      Status MakeNB(Ctx*, const ::Bytes* request, ::Block* response);
      Status Get(Ctx*, const ::Address* request, ::BlockOrStatus* response);
      Status Update(Ctx*, const ::Block* request, ::Status* response);
      Status Insert(Ctx*, const ::Block* request, ::Status* response);
      Status Remove(Ctx*, const ::Address* request, ::Status* response);
      Status NBAddress(Ctx*, const ::Bytes* request, ::Address* response);
      Status UserKey(Ctx*, const ::Bytes* request, ::KeyOrStatus* response);
      Status UserName(Ctx*, const ::Key* request, ::BytesOrStatus* response);
    private:
      infinit::model::Model& _model;
      elle::reactor::Scheduler& _sched;
    };

    ::grpc::Status DoughnutImpl::MakeCHB(Ctx*, const ::CHBData* request, ::Block* response)
    {
      ::Status status;
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

    ::grpc::Status DoughnutImpl::MakeACB(Ctx*, const ::Empty* request, ::Block* response)
    {
      ::Status status;
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

    ::grpc::Status DoughnutImpl::MakeOKB(Ctx*, const ::Empty* request, ::Block* response)
    {
      ::Status status;

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

    ::grpc::Status DoughnutImpl::MakeNB(Ctx*, const ::Bytes* request, ::Block* response)
    {
      _sched.mt_run<void>("MakeNB", [&] {
          std::unique_ptr<model::blocks::Block> nb = std::make_unique<model::doughnut::NB>(
            dynamic_cast<model::doughnut::Doughnut&>(_model),
            request->data(),
            elle::Buffer());
          SerializerOut sout(response);
          sout.serialize_forward(nb);
      });
      return ::grpc::Status::OK;
    }

    ::grpc::Status DoughnutImpl::NBAddress(Ctx*, const ::Bytes* request, ::Address* response)
    {
      _sched.mt_run<void>("NBAddress", [&] {
          auto addr = infinit::model::doughnut::NB::address(
            dynamic_cast<model::doughnut::Doughnut&>(_model).keys().K(),
            request->data(),
            _model.version());
          response->set_address(std::string((const char*)addr.value(), 32));
      });
      return ::grpc::Status::OK;
    }

    ::grpc::Status DoughnutImpl::Get(Ctx*, const ::Address* request, ::BlockOrStatus* response)
    {
      ::Status status;
      _sched.mt_run<void>("Get", [&] {
          if (!exception_handler(status, [&] {
              auto block = _model.fetch(to_address(request->address()));
              SerializerOut sout(response->mutable_block());
              sout.serialize_forward(block);
              response->mutable_block()->set_data(block->data().string());
          }))
          {
            response->mutable_status()->CopyFrom(status);
          }
      });
      return ::grpc::Status::OK;
    }

    ::grpc::Status DoughnutImpl::Update(Ctx*, const ::Block* request, ::Status* response)
    {
      _sched.mt_run<void>("Update", [&] {
          exception_handler(*response, [&] {
              SerializerIn sin(request);
              sin.set_context<model::doughnut::Doughnut*>(
                dynamic_cast<model::doughnut::Doughnut*>(&_model));
              std::unique_ptr<model::blocks::Block> block;
              sin.serialize_forward(block);
              // force a seal
              if (auto mb = dynamic_cast<model::blocks::MutableBlock*>(block.get()))
              {
                mb->data(request->data());
              }
              try
              {
                _model.update(std::move(block));
              }
              catch (model::doughnut::ValidationFailed const&)
              { // maybe the acls where updated
                // this is a rare case so keep the path above optimized
                SerializerIn sin(request);
                sin.set_context<model::doughnut::Doughnut*>(
                  dynamic_cast<model::doughnut::Doughnut*>(&_model));
                std::unique_ptr<model::blocks::Block> block;
                sin.serialize_forward(block);
                if (auto acb = dynamic_cast<model::doughnut::ACB*>(block.get()))
                {
                  acb->data(request->data());
                  auto wp = acb->get_world_permissions();
                  acb->set_world_permissions(!wp.first, wp.second);
                  acb->set_world_permissions(wp.first, wp.second);
                  _model.update(std::move(block));
                }
                else
                  throw;
              }
          });
      });
      return ::grpc::Status::OK;
    }

    ::grpc::Status DoughnutImpl::Insert(Ctx*, const ::Block* request, ::Status* response)
    {
      _sched.mt_run<void>("Insert", [&] {
          exception_handler(*response, [&] {
              SerializerIn sin(request);
              sin.set_context<model::doughnut::Doughnut*>(
                dynamic_cast<model::doughnut::Doughnut*>(&_model));
              std::unique_ptr<model::blocks::Block> block;
              sin.serialize_forward(block);
              // force a seal
              if (auto mb = dynamic_cast<model::blocks::MutableBlock*>(block.get()))
              {
                mb->data(request->data());
              }
              ELLE_DEBUG("insert %s with %s", block->address(), block->data());
              _model.insert(std::move(block));
          });
      });
      return ::grpc::Status::OK;
    }

    ::grpc::Status DoughnutImpl::Remove(Ctx*, const ::Address* request, ::Status* response)
    {
      _sched.mt_run<void>("Remove", [&] {
          exception_handler(*response, [&] {
              _model.remove(to_address(request->address()));
          });
      });
      return ::grpc::Status::OK;
    }

    ::grpc::Status DoughnutImpl::UserKey(Ctx*, const ::Bytes* request, ::KeyOrStatus* response)
    {
      ::Status status;
      _sched.mt_run<void>("UserKey", [&] {
         if (!exception_handler(status, [&] {
             auto user = _model.make_user(elle::Buffer(request->data()));
             auto const& key = dynamic_cast<model::doughnut::User&>(*user).key();
             elle::Option<elle::cryptography::rsa::PublicKey, int, bool> opt(key);
             SerializerOut sout(response->mutable_key());
             sout.serialize_forward(opt);
          }))
         {
           ELLE_ERR("exception: %s", status.message());
           response->mutable_status()->CopyFrom(status);
         }
      });
      return ::grpc::Status::OK;
    }

    ::grpc::Status DoughnutImpl::UserName(Ctx*, const ::Key* request, ::BytesOrStatus* response)
    {
      ::Status status;
      _sched.mt_run<void>("UserName", [&] {
          if (!exception_handler(status, [&] {
             SerializerIn sin(&request->value());
             elle::cryptography::rsa::PublicKey key(sin);
             auto user = _model.make_user(elle::serialization::json::serialize(key));
             response->mutable_bytes()->set_data(user->name());
          }))
         {
           ELLE_ERR("exception: %s", status.message());
           response->mutable_status()->CopyFrom(status);
         }
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