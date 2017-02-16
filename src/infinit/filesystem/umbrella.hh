#pragma once

#include <reactor/exception.hh>
#include <reactor/filesystem.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>
#include <infinit/storage/InsufficientSpace.hh>

#ifdef INFINIT_LINUX
# include <attr/xattr.h>
#endif

#ifdef INFINIT_WINDOWS
# define ENOATTR ENODATA
#endif

#define THROW_ACCES()  throw rfs::Error(EACCES,  "Access denied")
#define THROW_ENOSPC() throw rfs::Error(ENOSPC,  "No space left on device")
#define THROW_EXIST()  throw rfs::Error(EEXIST,  "File exists")
#define THROW_INVAL()  throw rfs::Error(EINVAL,  "Invalid argument")
#define THROW_ISDIR()  throw rfs::Error(EISDIR,  "Is a directory")
#define THROW_NOATTR() throw rfs::Error(ENOATTR, "No attribute")
#define THROW_NODATA() throw rfs::Error(ENODATA, "No data")
#define THROW_NOENT()  throw rfs::Error(ENOENT,  "No such file or directory")
#define THROW_NOSYS()  throw rfs::Error(ENOSYS,  "Not implemented")
#define THROW_NOTDIR() throw rfs::Error(ENOTDIR, "Is not a directory")

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = reactor::filesystem;

    using Block = infinit::model::blocks::Block;
    using MutableBlock = infinit::model::blocks::MutableBlock;
    using ImmutableBlock = infinit::model::blocks::ImmutableBlock;
    using ACLBlock = infinit::model::blocks::ACLBlock;
    using Address = infinit::model::Address;

    template <typename F>
    auto umbrella(F f, int err = EIO)
      -> decltype(f())
    {
      ELLE_LOG_COMPONENT("infinit.fs");
      try {
        return f();
      }
      catch(reactor::Terminate const& e)
      {
        throw;
      }
      catch (infinit::model::doughnut::ValidationFailed const& e)
      {
        ELLE_TRACE("perm exception %s", e);
        throw rfs::Error(EACCES, elle::sprintf("%s", e));
      }
      catch (infinit::storage::InsufficientSpace const& e)
      {
        ELLE_TRACE("umbrella: %s", e);
        THROW_ENOSPC();
      }
      catch (rfs::Error const& e)
      {
        ELLE_TRACE("rethrowing rfs exception: %s", e);
        throw;
      }
      catch(elle::Exception const& e)
      {
        ELLE_WARN("unexpected elle::exception %s", e);
        throw rfs::Error(err, elle::sprintf("%s", e));
      }
      catch(std::exception const& e)
      {
        ELLE_WARN("unexpected std::exception %s", e);
        throw rfs::Error(err, e.what());
      }
    }
  }
}
