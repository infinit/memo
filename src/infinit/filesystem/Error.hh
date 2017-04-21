#pragma once

#if defined INFINIT_LINUX
# include <sys/types.h>
# include <attr/xattr.h>
#elif defined INFINIT_MACOSX
# include <sys/xattr.h>
#endif

#ifndef  ENOATTR
# define ENOATTR ENODATA
#endif

#include <elle/compiler.hh>
#include <elle/printf.hh>
#include <elle/reactor/exception.hh>
#include <elle/reactor/filesystem.hh> // rfs::Error

#define THROW_ACCES()  infinit::filesystem::err(EACCES)
#define THROW_ENOSPC() infinit::filesystem::err(ENOSPC)
#define THROW_EXIST()  infinit::filesystem::err(EEXIST)
#define THROW_INVAL()  infinit::filesystem::err(EINVAL)
#define THROW_ISDIR()  infinit::filesystem::err(EISDIR)
#define THROW_NOATTR() infinit::filesystem::err(ENOATTR)
#define THROW_NODATA() infinit::filesystem::err(ENODATA)
#define THROW_NOENT()  infinit::filesystem::err(ENOENT)
#define THROW_NOSYS()  infinit::filesystem::err(ENOSYS)
#define THROW_NOTDIR() infinit::filesystem::err(ENOTDIR)

namespace infinit
{
  namespace filesystem
  {
    namespace rfs = elle::reactor::filesystem;

    /// Build an Error from msg if not empty, otherwise from errnum.
    rfs::Error
    make_error(int errnum, std::string const& msg = {});

    /// Raise an Error from errnum.
    ELLE_COMPILER_ATTRIBUTE_NORETURN
    inline
    void
    err(int errnum)
    {
      throw make_error(errnum);
    }

    /// Raise an Error from errnum.
    template <typename... Args>
    ELLE_COMPILER_ATTRIBUTE_NORETURN
    void
    err(int errnum, Args&&... args)
    {
      throw make_error(errnum,
                       elle::sprintf(std::forward<Args>(args)...));
    }
  }
}
