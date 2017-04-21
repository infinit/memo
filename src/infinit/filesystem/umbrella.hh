#pragma once

#include <elle/reactor/exception.hh>
#include <elle/reactor/filesystem.hh>

#include <infinit/filesystem/Error.hh>
#include <infinit/model/Address.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/storage/InsufficientSpace.hh>

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = elle::reactor::filesystem;

    using Block = infinit::model::blocks::Block;
    using MutableBlock = infinit::model::blocks::MutableBlock;
    using ImmutableBlock = infinit::model::blocks::ImmutableBlock;
    using ACLBlock = infinit::model::blocks::ACLBlock;
    using Address = infinit::model::Address;

    template <typename F>
    auto umbrella(F f, int errnum = EIO)
      -> decltype(f())
    {
      ELLE_LOG_COMPONENT("infinit.fs");
      try
      {
        return f();
      }
      catch (elle::reactor::Terminate const& e)
      {
        throw;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception %s", e);
        err(EACCES, "%s", e);
      }
      catch (infinit::storage::InsufficientSpace const& e)
      {
        ELLE_TRACE("umbrella: %s", e);
        err(ENOSPC);
      }
      catch (rfs::Error const& e)
      {
        ELLE_TRACE("rethrowing rfs exception: %s", e);
        throw;
      }
      catch (elle::Exception const& e)
      {
        ELLE_WARN("unexpected elle::exception %s", e);
        err(errnum, "%s", e);
      }
      catch (std::exception const& e)
      {
        ELLE_WARN("unexpected std::exception %s", e);
        err(errnum, "%s", e.what());
      }
    }
  }
}
