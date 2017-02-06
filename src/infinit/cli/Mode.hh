#pragma once

namespace infinit
{
  namespace cli
  {
    template <typename Super>
    struct Mode
      : public Super
    {
      template <typename ... Args>
      Mode(std::string help, das::cli::Options opts, Args&& ... args)
        : Super(std::forward<Args>(args)...)
        , description(std::move(help))
        , options(std::move(opts))
      {}
      std::string description;
      das::cli::Options options;
    };
  }
}
