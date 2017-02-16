#pragma once

#include <das/cli.hh>

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
      Mode<decltype(binding(modes::mode_create,
                            cli::network,
                            cli::user,
                            cli::push_passport = false,
                            cli::push = false,
                            cli::deny_write = false,
                            cli::deny_storage = false,
                            cli::allow_create_passport = false,
                            cli::output = boost::none))>
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
      Mode<decltype(binding(modes::mode_delete,
                            cli::network,
                            cli::user,
                            cli::pull = false))>
      delete_;
      void
      mode_delete(std::string const& network_name,
                  std::string const& user_name,
                  bool pull = false);

      // Export.
      Mode<decltype(binding(modes::mode_export,
                            cli::network,
                            cli::user,
                            cli::output = boost::none))>
      export_;
      void
      mode_export(std::string const& network_name,
                  std::string const& user_name,
                  boost::optional<std::string> const& output = {});

      // Fetch.
      Mode<decltype(binding(modes::mode_fetch,
                            cli::network = boost::none,
                            cli::user = boost::none))>
      fetch;
      void
      mode_fetch(boost::optional<std::string> network_name = {},
                 boost::optional<std::string> const& user_name = {});

      // Import.
      Mode<decltype(binding(modes::mode_import,
                            cli::input = boost::none))>
      import;
      void
      mode_import(boost::optional<std::string> const& input = {});

      // List.
      Mode<decltype(binding(modes::mode_list,
                            cli::network = boost::none))>
      list;
      void
      mode_list(boost::optional<std::string> network_name = {});

      // Pull.
      Mode<decltype(binding(modes::mode_pull,
                            cli::network,
                            cli::user))>
      pull;
      void
      mode_pull(std::string const& network_name,
                std::string const& user_name);

      // Push.
      Mode<decltype(binding(modes::mode_push,
                            cli::network,
                            cli::user))>
      push;
      void
      mode_push(std::string const& network_name,
                std::string const& user_name);
    };
  }
}
