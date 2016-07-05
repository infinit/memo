
template<typename F, typename ... Args>
void
check(F func, Args ... args)
{
  int res = func(args...);
  if (res < 0)
  {
    int error_number = errno;
    auto* e = std::strerror(error_number);
    if (error_number == EINVAL)
      throw InvalidArgument(std::string(e));
    else if (error_number == EACCES)
      throw PermissionDenied(std::string(e));
    else
      throw elle::Error(std::string(e));
  }
}
