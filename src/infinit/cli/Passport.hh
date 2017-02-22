#pragma once

#include <elle/das/cli.hh>

#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class Passport
      : public Object<Passport>
    {
    public:
      Passport(Infinit& infinit);
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
           decltype(modes::mode_create),
           decltype(cli::network),
           decltype(cli::user),
           decltype(cli::push_passport = false),
           decltype(cli::push = false),
           decltype(cli::deny_write = false),
           decltype(cli::deny_storage = false),
           decltype(cli::allow_create_passport = false),
           decltype(cli::output = boost::none)>
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
           decltype(modes::mode_delete),
           decltype(cli::network),
           decltype(cli::user),
           decltype(cli::pull = false)>
      delete_;
      void
      mode_delete(std::string const& network_name,
                  std::string const& user_name,
                  bool pull = false);

      // Export.
      Mode<Passport,
           decltype(modes::mode_export),
           decltype(cli::network),
           decltype(cli::user),
           decltype(cli::output = boost::none)>
      export_;
      void
      mode_export(std::string const& network_name,
                  std::string const& user_name,
                  boost::optional<std::string> const& output = {});

      // Fetch.
      Mode<Passport,
           decltype(modes::mode_fetch),
           decltype(cli::network = boost::none),
           decltype(cli::user = boost::none)>
      fetch;
      void
      mode_fetch(boost::optional<std::string> network_name = {},
                 boost::optional<std::string> const& user_name = {});

      // Import.
      Mode<Passport,
           decltype(modes::mode_import),
           decltype(cli::input = boost::none)>
      import;
      void
      mode_import(boost::optional<std::string> const& input = {});

      // List.
      Mode<Passport,
           decltype(modes::mode_list),
           decltype(cli::network = boost::none)>
      list;
      void
      mode_list(boost::optional<std::string> network_name = {});

      // Pull.
      Mode<Passport,
           decltype(modes::mode_pull),
           decltype(cli::network),
           decltype(cli::user)>
      pull;
      void
      mode_pull(std::string const& network_name,
                std::string const& user_name);

      // Push.
      Mode<Passport,
           decltype(modes::mode_push),
           decltype(cli::network),
           decltype(cli::user)>
      push;
      void
      mode_push(std::string const& network_name,
                std::string const& user_name);
    };
  }
}
