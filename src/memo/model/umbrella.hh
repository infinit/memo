#pragma once

#include <elle/compiler.hh>
#include <elle/printf.hh>
#include <elle/reactor/exception.hh>
#include <elle/reactor/exception.hh>
#include <elle/reactor/filesystem.hh>

#include <memo/filesystem/Error.hh>
#include <memo/model/Address.hh>
#include <memo/model/blocks/ACLBlock.hh>
#include <memo/model/blocks/ImmutableBlock.hh>
#include <memo/model/blocks/MutableBlock.hh>
#include <memo/model/doughnut/ValidationFailed.hh>
#include <memo/silo/InsufficientSpace.hh>

namespace memo
{
  namespace filesystem
  {
    namespace rfs = elle::reactor::filesystem;

    template <typename F>
    auto umbrella(F f, int errnum = EIO)
      -> decltype(f())
    {
      ELLE_LOG_COMPONENT("memo.fs");
      try
      {
        return f();
      }
      catch (elle::reactor::Terminate const& e)
      {
        throw;
      }
      catch (memo::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception %s", e);
        err(EACCES, "%s", e);
      }
      catch (memo::silo::InsufficientSpace const& e)
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
