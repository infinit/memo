#pragma once

#include <elle/das/cli.hh>

#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

namespace memo
{
  namespace cli
  {
    class Passport
      : public Object<Passport>
    {
    public:
      Passport(Memo& memo);
      using Modes
        = decltype(elle::meta::list(cli::create,
                                    cli::delete_,
                                    cli::export_,
                                    cli::fetch,
                                    cli::import,
                                    cli::list,
                                    cli::pull,
                                    cli::push));

      // Create.
      Mode<Passport,
           void (decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>,
                 decltype(cli::push_passport = false),
                 decltype(cli::push = false),
                 decltype(cli::deny_write = false),
                 decltype(cli::deny_storage = false),
                 decltype(cli::allow_create_passport = false),
                 decltype(cli::output = boost::optional<std::string>())),
           decltype(modes::mode_create)>
      create;
      void
      mode_create(std::string const& network_name,
                  std::string const& user_name,
                  bool push_passport = false,
                  bool push = false,
                  bool deny_write = false,
                  bool deny_storage = false,
                  bool allow_create_passport = false,
                  boost::optional<std::string> const& output = {});

      // Delete.
      Mode<Passport,
           void (decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>,
                 decltype(cli::pull = false)),
           decltype(modes::mode_delete)>
      delete_;
      void
      mode_delete(std::string const& network_name,
                  std::string const& user_name,
                  bool pull = false);

      // Export.
      Mode<Passport,
           void (decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>,
                 decltype(cli::output = boost::optional<std::string>())),
           decltype(modes::mode_export)>
      export_;
      void
      mode_export(std::string const& network_name,
                  std::string const& user_name,
                  boost::optional<std::string> const& output = {});

      // Fetch.
      Mode<Passport,
           void (decltype(cli::network = boost::optional<std::string>()),
                 decltype(cli::user = boost::optional<std::string>())),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> network_name = {},
                 boost::optional<std::string> const& user_name = {});

      // Import.
      Mode<Passport,
           void (decltype(cli::input = boost::optional<std::string>())),
           decltype(modes::mode_import)>
      import;
      void
      mode_import(boost::optional<std::string> const& input = {});

      // List.
      Mode<Passport,
           void (decltype(cli::network = boost::optional<std::string>())),
           decltype(modes::mode_list)>
      list;
      void
      mode_list(boost::optional<std::string> network_name = {});

      // Pull.
      Mode<Passport,
           void (decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>),
           decltype(modes::mode_pull)>
      pull;
      void
      mode_pull(std::string const& network_name,
                std::string const& user_name);

      // Push.
      Mode<Passport,
           void (decltype(cli::network)::Formal<std::string const&>,
                 decltype(cli::user)::Formal<std::string const&>),
           decltype(modes::mode_push)>
      push;
      void
      mode_push(std::string const& network_name,
                std::string const& user_name);
    };
  }
}
