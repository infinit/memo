#pragma once

#include <elle/meta.hh>

#include <elle/das/bound-method.hh>
#include <elle/das/named.hh>

#include <memo/cli/fwd.hh>

#include <memo/Memo.hh>
#include <memo/symbols.hh>

#include <memo/cli/Error.hh>

#include <memo/cli/Block.hh>
#include <memo/cli/Credentials.hh>
#if MEMO_WITH_DAEMON
# include <memo/cli/Daemon.hh>
#endif
#include <memo/cli/Device.hh>
#include <memo/cli/Doctor.hh>
#include <memo/cli/Journal.hh>
#if MEMO_WITH_KEY_VALUE_STORE
# include <memo/cli/KeyValueStore.hh>
#endif
#include <memo/cli/Network.hh>
#include <memo/cli/Passport.hh>
#include <memo/cli/Silo.hh>
#include <memo/cli/User.hh>

namespace memo
{
  namespace cli
  {
    using MemoCallable =
      elle::das::named::Function<
        void (decltype(help = false), decltype(version = false))>;

    class Memo
      : public elle::Printable::as<Memo>
      , public MemoCallable
    {
    public:
      Memo(memo::Memo& memo);

      // Helpers
      memo::User
      as_user();
      static
      void
      usage(std::ostream& s, std::string const& usage);
      std::unique_ptr<std::istream, std::function<void (std::istream*)>>
      get_input(boost::optional<std::string> const& path);
      std::unique_ptr<std::ostream, std::function<void (std::ostream*)>>
      get_output(boost::optional<std::string> path = {},
                 bool stdout_def = true);
      boost::optional<boost::filesystem::path>
      avatar_path(std::string const& name) const;
      virtual
      std::string
      default_user_name() const;
      memo::User
      default_user();

      // Report
      void
      report(std::string const& msg);
      template <typename... Args>
      /// Report using a printf-format.
      void
      report(std::string const& format, Args&&... args);
      /// Report action with the following formatting:
      /// (<where> )<action> <type> <name>.
      ///
      /// XXX: Consider using an enum for argument 'action' to prevent typos.
      void
      report_action(std::string const& action,
                    std::string const& type,
                    std::string const& name,
                    std::string const& where = {});
      /// Report the import of resource.
      ///
      /// This function bounce to report_action with action = "imported".
      void
      report_imported(std::string const& type, std::string const& name);
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
#if MEMO_WITH_DAEMON
      Daemon daemon = *this;
#endif
      Device device = *this;
      Doctor doctor = *this;
      Journal journal = *this;
#if MEMO_WITH_KEY_VALUE_STORE
      KeyValueStore kvs = *this;
#endif
      Network network{*this};
      Passport passport = *this;
      Silo silo = *this;
      User user = *this;
      using Objects
        = decltype(elle::meta::list(cli::block,
                                    cli::credentials,
#if MEMO_WITH_DAEMON
                                    cli::daemon,
#endif
                                    cli::device,
                                    cli::doctor,
                                    cli::journal,
#if MEMO_WITH_KEY_VALUE_STORE
                                    cli::kvs,
#endif
                                    cli::network,
                                    cli::passport,
                                    cli::silo,
                                    cli::user));
      virtual
      void
      help(std::ostream& s) const;
      void
      call(bool help, bool version) const;
      virtual
      void
      print(std::ostream& o) const;
      virtual
      memo::Memo&
      backend() const;
      ELLE_ATTRIBUTE(memo::Memo&, memo);
      ELLE_ATTRIBUTE_RW(std::vector<std::string>, command_line);
      ELLE_ATTRIBUTE_RW(boost::optional<std::string>, as);
      ELLE_ATTRIBUTE_RW(boost::optional<elle::Version>, compatibility_version);
      /// Whether in script mode.
      ELLE_ATTRIBUTE_RW(bool, script);
      /// Signal handler for termination.
      boost::signals2::signal<void ()> killed;
    };

    auto const options = elle::das::cli::Options
    {
      {"help", {'h', "show this help message"}},
      {"version", {'v', "show software version"}},
    };
  }
}

#include <memo/cli/Memo.hxx>
