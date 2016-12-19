#pragma once

#include <elle/meta.hh>

#include <das/bound-method.hh>
#include <das/named.hh>

#include <infinit/Infinit.hh>
#include <infinit/cli/Block.hh>
#include <infinit/cli/Credentials.hh>
#include <infinit/cli/Device.hh>
#include <infinit/cli/Drive.hh>
#include <infinit/cli/Silo.hh>
#include <infinit/cli/User.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    using InfinitCallable =
      decltype(das::named::function(
                 das::bind_method(std::declval<Infinit&>(), cli::call),
                 help = false, version = false));
    class Infinit
      : public InfinitCallable
    {
    public:
      Infinit(infinit::Infinit& infinit);

      // Helpers
      infinit::User
      as_user();
      static
      void
      usage(std::ostream& s, std::string const& usage);
      std::unique_ptr<std::istream, std::function<void (std::istream*)>>
      get_input(boost::optional<std::string> const& path);
      std::unique_ptr<std::ostream, std::function<void (std::ostream*)>>
      get_output(boost::optional<std::string> path = {},
                 bool stdout_def = true);
      boost::filesystem::path
      avatar_path() const;
      boost::optional<boost::filesystem::path>
      avatar_path(std::string const& name) const;
      static
      std::string
      default_user_name();
      infinit::User
      default_user();

      // Report
      void
      report(std::string const& msg);
      template <typename... Args>
      /// Report using a printf-format.
      void
      report(std::string const& format, Args&&... args);
      void
      report_action(std::string const& action,
                    std::string const& type,
                    std::string const& name,
                    std::string const& where = {});
      void
      report_created(std::string const& type, std::string const& name);
      void
      report_updated(std::string const& type, std::string const& name);
      void
      report_imported(std::string const& type, std::string const& name);
      void
      report_saved(std::string const& type, std::string const& name);
      void
      report_action_output(std::ostream& output,
                           std::string const& action,
                           std::string const& type,
                           std::string const& name);
      void
      report_exported(std::ostream& output,
                      std::string const& type,
                      std::string const& name);

      // Password utilities

      /// Read some secret on stdin.
      ///
      /// \param prompt   the invitation string given to the user.
      /// \param regex    if non empty, read until it matches the result.
      static
      std::string
      read_secret(std::string const& prompt,
                  std::string const& regex = {});
      static
      std::string
      read_passphrase();
      static
      std::string
      read_password();
      static
      std::string
      hash_password(std::string const& password_,
                    std::string salt);
      static
      std::string
      hub_password_hash(std::string const& password);

      // Modes
      Block block = *this;
      Credentials credentials = *this;
      Device device = *this;
      Drive drive = *this;
      Silo silo = *this;
      User user = *this;
      using Entities
        = decltype(elle::meta::list(cli::block,
                                    cli::credentials,
                                    cli::device,
                                    cli::drive,
                                    cli::silo,
                                    cli::user));
      void
      help(std::ostream& s) const;
      void
      call(bool help, bool version) const;
      ELLE_ATTRIBUTE_R(infinit::Infinit&, infinit);
      ELLE_ATTRIBUTE_R(boost::optional<std::string>, as);
      ELLE_ATTRIBUTE_R(boost::optional<elle::Version>, compatibility_version);
      /// Whether in script mode.
      ELLE_ATTRIBUTE_R(bool, script);
    private:
      template <typename Symbol, typename ObjectSymbol>
      friend struct mode;
    };
  }
}

#include <infinit/cli/Infinit.hxx>
