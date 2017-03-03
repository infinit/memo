#include <infinit/grpc/grpc.hh>

#include <infinit/model/MissingBlock.hh>
#include <infinit/model/Model.hh>

#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <infinit/model/doughnut/Conflict.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/User.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>

# include <elle/athena/paxos/Client.hh>

#include <infinit/grpc/kv.grpc.pb.h>

ELLE_LOG_COMPONENT("infinit.grpc.kv");

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
      Get(const ::KVAddress& request);
      ::KVStatus
      Insert(const ::KVBlock& request);
      ::KVStatus
      Update(const ::KVBlock& request);
      ::KVStatus
      Remove(const ::KVAddress& request);

      ::KVStatus
      Set(const ::KVBlock& request, infinit::model::StoreMode mode);
      ELLE_ATTRIBUTE(infinit::model::Model&, model);
    };

    // This is what we would do if we were using the sync API (which uses a thread pool)
    class KVBounce: public KV::Service
    {
    public:
      KVBounce(std::unique_ptr<KVImpl> impl, elle::reactor::Scheduler& sched)
      : _impl(std::move(impl))
      , _sched(sched)
      {}
      ::grpc::Status Get(::grpc::ServerContext* context, const ::KVAddress* request, ::BlockStatus* response)
      {
        _sched.mt_run<void>("Get", [&] {
            *response = std::move(_impl->Get(*request));
        });
        return ::grpc::Status::OK;
      }
      ::grpc::Status Insert(::grpc::ServerContext* context, const ::KVBlock* request, ::KVStatus* response)
      {
        _sched.mt_run<void>("Insert", [&] {
            *response = std::move(_impl->Insert(*request));
        });
        return ::grpc::Status::OK;
      }
      ::grpc::Status Update(::grpc::ServerContext* context, const ::KVBlock* request, ::KVStatus* response)
      {
        _sched.mt_run<void>("Update", [&] {
            *response = std::move(_impl->Update(*request));
        });
        return ::grpc::Status::OK;
      }
      ::grpc::Status Remove(::grpc::ServerContext* context, const ::KVAddress * request, ::KVStatus* response)
      {
        _sched.mt_run<void>("Remove", [&] {
            *response = std::move(_impl->Remove(*request));
        });
        return ::grpc::Status::OK;
      }
      std::unique_ptr<KVImpl> _impl;
      elle::reactor::Scheduler& _sched;
    };

    bool exception_handler(::KVStatus& res, std::function<void()> f)
    {
      try
      {
        f();
        ELLE_TRACE("no exception encountered");
        return true;
      }
      catch (model::MissingBlock const& err)
      {
        res.set_error(KV_ERROR_MISSING_BLOCK);
        res.set_message(err.what());
      }
      catch (model::doughnut::ValidationFailed const& err)
      {
        res.set_error(KV_ERROR_VALIDATION_FAILED);
        res.set_message(err.what());
      }
      catch (elle::athena::paxos::TooFewPeers const& err)
      {
        res.set_error(KV_ERROR_TOO_FEW_PEERS);
        res.set_message(err.what());
      }
      catch (model::doughnut::Conflict const& err)
      {
        res.set_error(KV_ERROR_CONFLICT);
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
          res.set_error(KV_ERROR_NO_PEERS);
        else
          res.set_error(KV_ERROR_OTHER);
        res.set_message(err.what());
      }
      ELLE_TRACE("an exception was encountered");
      return false;
    }

    ::BlockStatus
    KVImpl::Get(const ::KVAddress& request)
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
          ::KVACL* p = abd->add_permissions();
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

      res.mutable_status()->set_error(KV_ERROR_OK);
      res.mutable_status()->set_message(std::string());
      return res;
    }

    ::KVStatus
    KVImpl::Insert(const ::KVBlock& request)
    {
      return Set(request, infinit::model::STORE_INSERT);
    }

    ::KVStatus
    KVImpl::Update(const ::KVBlock& request)
    {
      return Set(request, infinit::model::STORE_UPDATE);
    }

    ::KVStatus
    KVImpl::Set(const ::KVBlock& request, infinit::model::StoreMode mode)
    {
      auto const& iblock = request;
      ELLE_LOG("Set %s", iblock.address());
      ::KVStatus res;
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
          res.set_error(KV_ERROR_CONFLICT);
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
        ablock->set_world_permissions(ra.world_read(), ra.world_write());
        for (int i=0; i< ra.permissions_size(); ++i)
        {
          auto const& p = ra.permissions(i);
          auto user = _model.make_user(elle::Buffer(p.user()));
          ablock->set_permissions(*user, p.read(), p.write());
        }
        if (ra.version() > 0)
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
      res.set_error(KV_ERROR_OK);
      res.set_message(std::string());
      exception_handler(res,
        [&] {
          if (mode == infinit::model::STORE_INSERT)
            _model.insert(std::move(block));
          else
            _model.update(std::move(block));
        });
      return res;
    }

    ::KVStatus
    KVImpl::Remove(::KVAddress const& address)
    {
      ::KVStatus res;
      res.set_error(KV_ERROR_OK);
      res.set_message(std::string());
      exception_handler(res,
        [&] {
        _model.remove(model::Address::from_string(address.address()));
      });
      return res;
    }

    std::unique_ptr< ::grpc::Service>
    kv_service(model::Model& dht)
    {
      auto impl = std::make_unique<KVImpl>(dht);
      return std::make_unique<KVBounce>(std::move(impl), elle::reactor::scheduler());
    }
  }
}