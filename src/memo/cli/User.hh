#pragma once

#include <boost/optional.hpp>

#include <elle/das/cli.hh>

#include <memo/User.hh>
#include <memo/cli/Object.hh>
#include <memo/cli/Mode.hh>
#include <memo/cli/fwd.hh>
#include <memo/cli/symbols.hh>
#include <memo/symbols.hh>

namespace memo
{
  namespace cli
  {
    using PrivateUserPublish = elle::das::Model<
      memo::User,
      decltype(elle::meta::list(
                 memo::symbols::name,
                 memo::symbols::description,
                 memo::symbols::email,
                 memo::symbols::fullname,
                 memo::symbols::public_key,
                 memo::symbols::private_key,
                 memo::symbols::ldap_dn))>;

    class User
      : public Object<User>
    {
    public:
      User(Memo& memo);
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
      Mode<User,
           void (decltype(cli::name = std::string{}),
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::key = boost::optional<std::string>()),
                 decltype(cli::email = boost::optional<std::string>()),
                 decltype(cli::fullname = boost::optional<std::string>()),
                 decltype(cli::password = boost::optional<std::string>()),
                 decltype(cli::ldap_name = boost::optional<std::string>()),
                 decltype(cli::output = boost::optional<std::string>()),
                 decltype(cli::push_user = false),
                 decltype(cli::push = false),
                 decltype(cli::full = false)),
           decltype(modes::mode_create)>
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
      Mode<User,
           void (decltype(cli::name = std::string{}),
                 decltype(cli::pull = false),
                 decltype(cli::purge = false),
                 decltype(cli::force = false)),
           decltype(modes::mode_delete)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge,
                  bool force);

      // Export
      Mode<User,
           void (decltype(cli::name = std::string{}),
                 decltype(cli::full = false),
                 decltype(cli::output = boost::optional<std::string>())),
           decltype(modes::mode_export)>
      export_;
      void
      mode_export(std::string const& name,
                  bool full,
                  boost::optional<std::string> path);

      // Fetch
      Mode<User,
           void (decltype(cli::name = std::vector<std::string>{}),
                 decltype(cli::no_avatar = false)),
           decltype(modes::mode_fetch)>
      fetch;
      void
      mode_fetch(std::vector<std::string> const& names,
                 bool no_avatar);

      // Hash
      Mode<User,
           void (decltype(cli::name = std::string{})),
           decltype(modes::mode_hash)>
      hash;
      void
      mode_hash(std::string const& name);

      // Import
      Mode<User,
           void (decltype(cli::input = boost::optional<std::string>())),
           decltype(modes::mode_import)>
      import;
      void
      mode_import(boost::optional<std::string> const& input);

      // List
      Mode<User,
           void (),
           decltype(modes::mode_list)>
      list;
      void
      mode_list();

      // Login
      Mode<User,
           void (decltype(cli::name = std::string{}),
                 decltype(cli::password = boost::optional<std::string>())),
           decltype(modes::mode_login)>
      login;
      void
      mode_login(std::string const& name,
                 boost::optional<std::string> const& password);

      // Pull
      Mode<User,
           void (decltype(cli::name = std::string{}),
                 decltype(cli::purge = false)),
           decltype(modes::mode_pull)>
      pull;
      void
      mode_pull(std::string const& name, bool purge);

      // Push
      Mode<User,
           void (decltype(cli::name = std::string{}),
                 decltype(cli::email = boost::optional<std::string>()),
                 decltype(cli::fullname = boost::optional<std::string>()),
                 decltype(cli::password = boost::optional<std::string>()),
                 decltype(cli::avatar = boost::optional<std::string>()),
                 decltype(cli::full = false)),
           decltype(modes::mode_push)>
      push;
      void
      mode_push(std::string const& name,
                boost::optional<std::string> email,
                boost::optional<std::string> fullname,
                boost::optional<std::string> password,
                boost::optional<std::string> avatar,
                bool full);

      // Signup
      Mode<User,
           void (decltype(cli::name = std::string{}),
                 decltype(cli::description = boost::optional<std::string>()),
                 decltype(cli::key = boost::optional<std::string>()),
                 decltype(cli::email = boost::optional<std::string>()),
                 decltype(cli::fullname = boost::optional<std::string>()),
                 decltype(cli::password = boost::optional<std::string>()),
                 decltype(cli::ldap_name = boost::optional<std::string>()),
                 decltype(cli::full = false)),
           decltype(modes::mode_signup)>
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
