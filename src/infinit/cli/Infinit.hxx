#pragma once

namespace infinit
{
  namespace cli
  {
    template <typename... Args>
    void
    Infinit::report(std::string const& format, Args&&... args)
    {
      report(elle::sprintf(format, std::forward<Args>(args)...));
    }
  }
}
