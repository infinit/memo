#pragma once

#include <boost/optional.hpp>

#include <das/cli.hh>

#include <infinit/User.hh>
#include <infinit/cli/Entity.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class User
      : public Entity<User>
    {
    public:
      User(Infinit& infinit);
      using Modes = decltype(elle::meta::list(
                               cli::create,
                               cli::delete_,
                               cli::export_,
                               cli::hash,
                               cli::import,
                               cli::fetch,
                               cli::list,
                               cli::login,
                               cli::pull,
                               cli::push,
                               cli::signup));
      // Create
      Mode<decltype(binding(modes::mode_create,
                            name = std::string{},
                            description = boost::none,
                            key = boost::none,
                            cli::email = boost::none,
                            fullname = boost::none,
                            password = boost::none,
                            ldap_name = boost::none,
                            output = boost::none,
                            push_user = false,
                            cli::push = false,
                            full = false))>
      create;
      void
      mode_create(std::string const& name,
                  boost::optional<std::string> description,
                  boost::optional<std::string> key,
                  boost::optional<std::string> email,
                  boost::optional<std::string> fullname,
                  boost::optional<std::string> password,
                  boost::optional<std::string> ldap_name,
                  boost::optional<std::string> path,
                  bool push_user,
                  bool push,
                  bool full);
      // Delete
      Mode<decltype(binding(modes::mode_delete,
                            name = std::string{},
                            cli::pull = false,
                            purge = false))>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge);
      // Export
      Mode<decltype(binding(modes::mode_export,
                            name = std::string{},
                            full = false,
                            output = boost::none))>
      export_;
      void
      mode_export(std::string const& name,
                  bool full,
                  boost::optional<std::string> path);
      // Fetch
      Mode<decltype(binding(modes::mode_fetch,
                            name = std::string{},
                            cli::no_avatar = false))>
      fetch;
      void
      mode_fetch(std::vector<std::string> const& names,
                 bool no_avatar);
      // Hash
      Mode<decltype(binding(modes::mode_hash,
                            name = std::string{}))>
      hash;
      void
      mode_hash(std::string const& name);
      // Import
      Mode<decltype(binding(modes::mode_import,
                            cli::input = boost::none))>
      import;
      void
      mode_import(boost::optional<std::string> const& input);
      // List
      Mode<decltype(binding(modes::mode_list))>
      list;
      void
      mode_list();
      // Login
      Mode<decltype(binding(modes::mode_login,
                            name = std::string{},
                            password = boost::none))>
      login;
      void
      mode_login(std::string const& name,
                 boost::optional<std::string> const& password);
      // Pull
      Mode<decltype(binding(modes::mode_pull,
                            name = std::string{},
                            purge = false))>
      pull;
      void
      mode_pull(std::string const& name, bool purge);
      // Push
      Mode<decltype(binding(modes::mode_push,
                            name  = std::string{},
                            cli::email = boost::none,
                            fullname = boost::none,
                            password = boost::none,
                            cli::avatar = boost::none,
                            full = false))>
      push;
      void
      mode_push(std::string const& name,
                boost::optional<std::string> email,
                boost::optional<std::string> fullname,
                boost::optional<std::string> password,
                boost::optional<std::string> avatar,
                bool full);
      // Signup
      Mode<decltype(binding(modes::mode_signup,
                            name = std::string{},
                            description = boost::none,
                            key = boost::none,
                            cli::email = boost::none,
                            fullname = boost::none,
                            password = boost::none,
                            ldap_name = boost::none,
                            full = false))>
      signup;
      void
      mode_signup(std::string const& name,
                  boost::optional<std::string> description,
                  boost::optional<std::string> key,
                  boost::optional<std::string> email,
                  boost::optional<std::string> fullname,
                  boost::optional<std::string> password,
                  boost::optional<std::string> ldap_name,
                  bool full);
    };
  }
}
