#pragma once

#include <elle/meta.hh>

#include <das/bound-method.hh>
#include <das/named.hh>

#include <infinit/cli/fwd.hh>

#include <infinit/Infinit.hh>
#include <infinit/cli/ACL.hh>
#include <infinit/cli/Block.hh>
#include <infinit/cli/Credentials.hh>
#if INFINIT_WITH_DAEMON
# include <infinit/cli/Daemon.hh>
#endif
#include <infinit/cli/Device.hh>
#include <infinit/cli/Doctor.hh>
#include <infinit/cli/Drive.hh>
#include <infinit/cli/Journal.hh>
#include <infinit/cli/LDAP.hh>
#include <infinit/cli/Network.hh>
#include <infinit/cli/Passport.hh>
#include <infinit/cli/Silo.hh>
#include <infinit/cli/User.hh>
#include <infinit/cli/Volume.hh>
#include <infinit/symbols.hh>

namespace infinit
{
  namespace cli
  {
    class CLIError
      : public das::cli::Error
    {
      using das::cli::Error::Error;
    };

    using InfinitCallable =
      decltype(das::named::function(
                 das::bind_method(std::declval<Infinit&>(), cli::call),
                 help = false, version = false));

    class Infinit
      : public elle::Printable::as<Infinit>
      , public InfinitCallable
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
      ACL acl = *this;
      Block block = *this;
      Credentials credentials = *this;
#if INFINIT_WITH_DAEMON
      Daemon daemon = *this;
#endif
      Device device = *this;
      Doctor doctor = *this;
      Drive drive = *this;
      Journal journal = *this;
      LDAP ldap = *this;
      Network network = *this;
      Passport passport = *this;
      Silo silo = *this;
      User user = *this;
      Volume volume = *this;
      using Objects
        = decltype(elle::meta::list(cli::acl,
                                    cli::block,
                                    cli::credentials,
#if INFINIT_WITH_DAEMON
                                    cli::daemon,
#endif
                                    cli::device,
                                    cli::doctor,
                                    cli::drive,
                                    cli::journal,
                                    cli::ldap,
                                    cli::network,
                                    cli::passport,
                                    cli::silo,
                                    cli::user,
                                    cli::volume));
      void
      help(std::ostream& s) const;
      void
      call(bool help, bool version) const;
      void
      print(std::ostream& o) const;
      ELLE_ATTRIBUTE_R(infinit::Infinit&, infinit);
      ELLE_ATTRIBUTE_R(boost::optional<std::string>, as);
      ELLE_ATTRIBUTE_R(boost::optional<elle::Version>, compatibility_version);
      /// Whether in script mode.
      ELLE_ATTRIBUTE_R(bool, script);
      /// Signal handler for termination.
      boost::signals2::signal<void ()> killed;

    private:
      template <typename Symbol, typename ObjectSymbol>
      friend struct mode_call;
    };
  }
}

#include <infinit/cli/Infinit.hxx>
