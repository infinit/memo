#pragma once

#include <boost/optional.hpp>

#include <elle/das/cli.hh>

#include <infinit/User.hh>
#include <infinit/cli/Object.hh>
#include <infinit/cli/Mode.hh>
#include <infinit/cli/fwd.hh>
#include <infinit/cli/symbols.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    using PrivateUserPublish = elle::das::Model<
      infinit::User,
      decltype(elle::meta::list(
                 infinit::symbols::name,
                 infinit::symbols::description,
                 infinit::symbols::email,
                 infinit::symbols::fullname,
                 infinit::symbols::public_key,
                 infinit::symbols::private_key,
                 infinit::symbols::ldap_dn))>;

    class User
      : public Object<User>
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
      Mode<User,
           decltype(modes::mode_create),
           decltype(cli::name = std::string{}),
           decltype(cli::description = boost::none),
           decltype(cli::key = boost::none),
           decltype(cli::email = boost::none),
           decltype(cli::fullname = boost::none),
           decltype(cli::password = boost::none),
           decltype(cli::ldap_name = boost::none),
           decltype(cli::output = boost::none),
           decltype(cli::push_user = false),
           decltype(cli::push = false),
           decltype(cli::full = false)>
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
           decltype(modes::mode_delete),
           decltype(cli::name = std::string{}),
           decltype(cli::pull = false),
           decltype(cli::purge = false),
           decltype(cli::force = false)>
      delete_;
      void
      mode_delete(std::string const& name,
                  bool pull,
                  bool purge,
                  bool force);

      // Export
      Mode<User,
           decltype(modes::mode_export),
           decltype(cli::name = std::string{}),
           decltype(cli::full = false),
           decltype(cli::output = boost::none)>
      export_;
      void
      mode_export(std::string const& name,
                  bool full,
                  boost::optional<std::string> path);

      // Fetch
      Mode<User,
           decltype(modes::mode_fetch),
           decltype(cli::name = std::string{}),
           decltype(cli::no_avatar = false)>
      fetch;
      void
      mode_fetch(std::vector<std::string> const& names,
                 bool no_avatar);

      // Hash
      Mode<User,
           decltype(modes::mode_hash),
           decltype(cli::name = std::string{})>
      hash;
      void
      mode_hash(std::string const& name);

      // Import
      Mode<User,
           decltype(modes::mode_import),
           decltype(cli::input = boost::none)>
      import;
      void
      mode_import(boost::optional<std::string> const& input);

      // List
      Mode<User,
           decltype(modes::mode_list)>
      list;
      void
      mode_list();

      // Login
      Mode<User,
           decltype(modes::mode_login),
           decltype(cli::name = std::string{}),
           decltype(cli::password = boost::none)>
      login;
      void
      mode_login(std::string const& name,
                 boost::optional<std::string> const& password);

      // Pull
      Mode<User,
           decltype(modes::mode_pull),
           decltype(cli::name = std::string{}),
           decltype(cli::purge = false)>
      pull;
      void
      mode_pull(std::string const& name, bool purge);

      // Push
      Mode<User,
           decltype(modes::mode_push),
           decltype(cli::name = std::string{}),
           decltype(cli::email = boost::none),
           decltype(cli::fullname = boost::none),
           decltype(cli::password = boost::none),
           decltype(cli::avatar = boost::none),
           decltype(cli::full = false)>
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
           decltype(modes::mode_signup),
           decltype(cli::name = std::string{}),
           decltype(cli::description = boost::none),
           decltype(cli::key = boost::none),
           decltype(cli::email = boost::none),
           decltype(cli::fullname = boost::none),
           decltype(cli::password = boost::none),
           decltype(cli::ldap_name = boost::none),
           decltype(cli::full = false)>
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
