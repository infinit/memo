#ifndef INFINIT_FILESYSTEM_UMBRELLA_HH
# define INFINIT_FILESYSTEM_UMBRELLA_HH

#include <reactor/exception.hh>
#include <reactor/filesystem.hh>
#include <infinit/model/doughnut/ValidationFailed.hh>
#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/ACLBlock.hh>

#ifdef INFINIT_LINUX
  #include <attr/xattr.h>
#endif

#ifdef INFINIT_WINDOWS
  #define ENOATTR ENODATA
#endif

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = reactor::filesystem;

    typedef infinit::model::blocks::Block Block;
    typedef infinit::model::blocks::MutableBlock MutableBlock;
    typedef infinit::model::blocks::ImmutableBlock ImmutableBlock;
    typedef infinit::model::blocks::ACLBlock ACLBlock;
    typedef infinit::model::Address Address;


    template<typename F>
    auto umbrella(F f, int err = EIO) -> decltype(f())
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

    #define THROW_NOENT { throw rfs::Error(ENOENT, "No such file or directory");}
    #define THROW_NOSYS { throw rfs::Error(ENOSYS, "Not implemented");}
    #define THROW_EXIST { throw rfs::Error(EEXIST, "File exists");}
    #define THROW_ISDIR { throw rfs::Error(EISDIR, "Is a directory");}
    #define THROW_NOTDIR { throw rfs::Error(ENOTDIR, "Is not a directory");}
    #define THROW_NODATA { throw rfs::Error(ENODATA, "No data");}
    #define THROW_NOATTR { throw rfs::Error(ENOATTR, "No attribute");}
    #define THROW_INVAL { throw rfs::Error(EINVAL, "Invalid argument");}
    #define THROW_ACCES { throw rfs::Error(EACCES, "Access denied");}
  }
}
#endif
