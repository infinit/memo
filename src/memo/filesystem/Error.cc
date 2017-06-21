#include <memo/filesystem/Error.hh>

namespace
{
  // FIXME: why don't we simply use strerror?
  std::string
  str_error(int errnum)
  {
    switch (errnum)
    {
#define CASE(Errnum, Msg)                        \
      case Errnum:                               \
        return Msg
      CASE(EACCES,  "Access denied");
      CASE(ENOSPC,  "No space left on device");
      CASE(EEXIST,  "File exists");
      CASE(EINVAL,  "Invalid argument");
      CASE(EISDIR,  "Is a directory");
#if ENOATTR != ENODATA
      CASE(ENOATTR, "No attribute");
#endif
      CASE(ENODATA, "No data");
      CASE(ENOENT,  "No such file or directory");
      CASE(ENOSYS,  "Not implemented");
      CASE(ENOTDIR, "Is not a directory");
#undef CASE
    }
    return "";
  }
}

namespace memo
{
  namespace filesystem
  {
    namespace rfs = elle::reactor::filesystem;
    rfs::Error
    make_error(int errnum, std::string const& msg)
    {
      return {errnum, msg.empty() ? str_error(errnum) : msg};
    }
  }
}
