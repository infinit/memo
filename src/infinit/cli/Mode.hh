#pragma once

namespace infinit
{
  namespace cli
  {
    template <typename Self, typename Symbol, typename ... Args>
    struct Mode
      : public das::named::Function<das::BoundMethod<Self, Symbol>,
                                    std::decay_t<Args>...>
    {
      using Super =
        das::named::Function<das::BoundMethod<Self, Symbol>,
                             std::decay_t<Args>...>;

      template <typename ... EArgs>
      Mode(Self& self,
           std::string help,
           das::cli::Options opts,
           EArgs&& ... args)
        : Super(das::bind_method<Symbol>(self), std::forward<EArgs>(args)...)
        , description(std::move(help))
        , options(std::move(opts))
      {}

      template <typename ... EArgs>
      Mode(Self& self, std::string help, EArgs&& ... args)
        : Mode(self, std::move(help), das::cli::Options(),
               std::forward<EArgs>(args)...)
      {}

      void
      apply(Infinit& infinit, std::vector<std::string>& args);

      std::string description;
      das::cli::Options options;
    };
  }
}
