#pragma once

namespace infinit
{
  namespace cli
  {
    template <typename Self, typename Symbol, typename ... Args>
    struct Mode
      : public elle::das::named::Function<elle::das::BoundMethod<Self, Symbol>,
                                    std::decay_t<Args>...>
    {
      using Super =
        elle::das::named::Function<elle::das::BoundMethod<Self, Symbol>,
                             std::decay_t<Args>...>;

      template <typename ... EArgs>
      Mode(Self& self,
           std::string help,
           elle::das::cli::Options opts,
           EArgs&& ... args)
        : Super(elle::das::bind_method<Symbol>(self), std::forward<EArgs>(args)...)
        , description(std::move(help))
        , options(std::move(opts))
      {}

      template <typename ... EArgs>
      Mode(Self& self, std::string help, EArgs&& ... args)
        : Mode(self, std::move(help), elle::das::cli::Options(),
               std::forward<EArgs>(args)...)
      {}

      void
      apply(Infinit& infinit, std::vector<std::string>& args);

      std::string description;
      elle::das::cli::Options options;
    };
  }
}
