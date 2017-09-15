#include <iostream>

#include <elle/err.hh>

#include <memo/cli/Credentials.hh>
#include <memo/cli/Memo.hh>
#include <memo/cli/utility.hh>

namespace memo
{
  namespace cli
  {
    namespace
    {
      template <typename T>
      struct ServicePrint
      {
        static const std::string name;
        static const std::string pretty;
      };
      using GCSPrint = ServicePrint<
        std::remove_const_t<decltype(cli::gcs)>
      >;
      template<>
      const std::string GCSPrint::name{"gcs"};
      template<>
      const std::string GCSPrint::pretty{"Google Cloud Storage"};
      using AWSPrint = ServicePrint<
        std::remove_const_t<decltype(cli::aws)>
      >;
      template<>
      const std::string AWSPrint::name{"aws"};
      template<>
      const std::string AWSPrint::pretty{"Amazon Web Services"};
      using DropboxPrint = ServicePrint<
        std::remove_const_t<decltype(cli::dropbox)>
      >;
      template<>
      const std::string DropboxPrint::name{"dropbox"};
      template<>
      const std::string DropboxPrint::pretty{"Dropbox"};
      using GoogleDrivePrint = ServicePrint<
        std::remove_const_t<decltype(cli::google_drive)>
      >;
      template<>
      const std::string GoogleDrivePrint::name{"google"};
      template<>
      const std::string GoogleDrivePrint::pretty{"Google Drive"};
    }

    Credentials::Credentials(Memo& memo)
      : Object(memo)
      , add(*this,
            "Add credentials for a third-party service",
            name,
            aws = false,
            dropbox = false,
            gcs = false,
            google_drive = false)
      , delete_(*this,
                "Delete locally credentials for a third-party service",
                name,
                aws = false,
                dropbox = false,
                gcs = false,
                google_drive = false,
                cli::pull = false)
      , fetch(*this,
              "Fetch credentials from {hub}",
              name = boost::none,
              aws = false,
              dropbox = false,
              gcs = false,
              google_drive = false)
      , pull(*this,
             "Pull credentials from {hub}",
             account = boost::none,
             aws = false,
             dropbox = false,
             gcs = false,
             google_drive = false)
      , list(*this,
             "List local credentials",
             aws = false,
             dropbox = false,
             gcs = false,
             google_drive = false)
    {}

    namespace
    {
      /// A structure easy to query using das symbols.
      ///
      /// E.g., `google.attr_get(enabled)` -> true/false, where
      /// `google` is a elle::das::Symbol.
      struct Enabled
      {
        bool
        all() const
        {
          return aws && dropbox && gcs && google_drive;
        }

        bool
        none() const
        {
          return !aws && !dropbox && !gcs && !google_drive;
        }

        bool
        several() const
        {
          return 1 < aws + dropbox + gcs + google_drive;
        }

        void
        all_if_none()
        {
          if (none())
            aws = dropbox = gcs = google_drive = true;
        }

        void
        ensure_at_least_one(std::string const& mode)
        {
          if (this->none())
            elle::err("%s: specify a service", mode);
        }

        bool aws;
        bool dropbox;
        bool gcs;
        bool google_drive;
      };
    }

    /*------------.
    | Mode: add.  |
    `------------*/

    namespace
    {
      /// Point to the web documentation to register a user on a given
      /// service provider.
      ///
      /// \param username user name
      /// \param service  service (e.g., "cli::google_drive")
      template <typename Service>
      void
      web_doc(std::string const& username,
              Service service)
      {
        std::cout << "Register your "
                  << ServicePrint<Service>::pretty
                  << " account with memo by visiting "
                  << memo::beyond() << "/users/" << username
                  << "/"
                  << ServicePrint<Service>::name
                  << "-oauth" << '\n';
      }
    }

    void
    Credentials::mode_add(std::string const& account,
                          bool aws,
                          bool dropbox,
                          bool gcs,
                          bool google_drive)
    {
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.ensure_at_least_one("add");
      if (aws)
      {
        std::cout << "Please enter your " << AWSPrint::pretty
                  << "  credentials\n";
        auto access_key_id
          = this->cli().read_secret("Access Key ID", "[A-Z0-9]{20}");
        auto secret_access_key
          = this->cli().read_secret("Secret Access Key", "[A-Za-z0-9/+=]{40}");
        auto aws_credentials =
          std::make_unique<memo::AWSCredentials>(account,
                                                    access_key_id,
                                                    secret_access_key);
        memo.credentials_aws_add(std::move(aws_credentials));
      }
      else if (dropbox)
        web_doc(owner.name, cli::dropbox);
      else if (gcs)
        web_doc(owner.name, cli::gcs);
      else if (google_drive)
        web_doc(owner.name, cli::google_drive);
    }

    /*---------------.
    | Mode: delete.  |
    `---------------*/

    namespace
    {
      template <typename Service>
      void
      pull_(memo::cli::Memo& cli,
            Service service,
            boost::optional<std::string> const& name,
            bool allow_missing);

      template <typename Service>
      void
      do_delete_(Credentials& cred,
                 Enabled const& enabled,
                 Service service,
                 std::string const& account_name,
                 bool pull = false)
      {
        if (service.attr_get(enabled))
        {
          auto& memo = cred.cli().backend();
          memo.credentials_delete(ServicePrint<Service>::name, account_name);
          if (pull)
            pull_(cred.cli(), service, account_name, true);
        }
      }
    }

    void
    Credentials::mode_delete(std::string const& account,
                             bool aws,
                             bool dropbox,
                             bool gcs,
                             bool google_drive,
                             bool pull)
    {
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.ensure_at_least_one("delete");
      do_delete_(*this, e, cli::aws,          account, pull);
      do_delete_(*this, e, cli::dropbox,      account, pull);
      do_delete_(*this, e, cli::google_drive, account, pull);
      do_delete_(*this, e, cli::gcs,          account, pull);
    }

    /*--------------.
    | Mode: fetch.  |
    `--------------*/

    namespace
    {
      using Cred = memo::OAuthCredentials;
      using UCred = std::unique_ptr<Cred>;

      /// Fetch credentials.
      ///
      /// \param user     the user
      /// \param service  the service (e.g., "cli::google_drive").
      /// \param add      function to call to add the fetched credentials
      /// \param account  name of a specific credentials to fetch
      template <typename Service>
      void
      fetch_(memo::Memo const& memo,
             memo::User const& user,
             Service service,
             std::function<void (UCred)> add,
             boost::optional<std::string> const& account)
      {
        auto where = elle::sprintf("users/%s/credentials/%s", user.name,
                                   ServicePrint<Service>::name);
        if (account)
          where += elle::sprintf("/%s", *account);
        // FIXME: Workaround for using std::unique_ptr.
        // Remove when serialization does not require copy.
        auto res =
          memo.hub_fetch_json
          (where,
           elle::sprintf("\"%s\" credentials", ServicePrint<Service>::pretty),
           user.name, user);
        auto root = boost::any_cast<elle::json::Object>(res);
        auto credentials_vec =
          boost::any_cast<std::vector<elle::json::Json>>(root["credentials"]);
        for (auto const& a_json: credentials_vec)
        {
          auto input = elle::serialization::json::SerializerIn(a_json, false);
          auto a = std::make_unique<Cred>(input.deserialize<Cred>());
          elle::fprintf(
            std::cout, "Fetched %s credentials \"%s\" (%s)\n",
            ServicePrint<Service>::pretty, a->uid(), a->display_name());
          add(std::move(a));
        }
      }
    }

    void
    Credentials::mode_fetch(boost::optional<std::string> const& account,
                            bool aws, bool dropbox,
                            bool gcs, bool google_drive)
    {
      auto& cli = this->cli();
      auto& memo = cli.backend();
      auto owner = cli.as_user();
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.all_if_none();
      bool fetch_all = e.all();
      // FIXME: Use Symbols instead.
      if (e.aws)
      {
        if (fetch_all)
          this->cli().report("INFO: %s credentials are not stored on %s"
                             " and so were not fetched",
                             AWSPrint::pretty,
                             memo::beyond(true));
        else
          elle::err<CLIError>("%s credentials are not stored on %s",
                              AWSPrint::pretty, memo::beyond(true));
      }
      if (e.dropbox)
        fetch_
          (memo, owner, cli::dropbox,
           [&memo] (UCred a)
           { memo.credentials_dropbox_add(std::move(a)); },
           account);
      if (e.gcs)
        fetch_
          (memo, owner, cli::gcs,
           [&memo] (UCred a)
           { memo.credentials_gcs_add(std::move(a)); },
           account);
      if (e.google_drive)
        fetch_
          (memo, owner, cli::google_drive,
           [&memo] (UCred a)
           { memo.credentials_google_add(std::move(a)); },
           account);
      // XXX: remove deleted ones
    }

    /*------------.
    | Mode: Pull. |
    `-------------*/

    namespace
    {
      using Cred = memo::OAuthCredentials;
      using UCred = std::unique_ptr<Cred>;

      /// Pull credentials.
      ///
      /// \param user          the user
      /// \param service       the service (e.g., "cli::google_drive").
      /// \param account       name of a specific credentials to remove (
      ///                      otherwise all the credentials for the service are
      ///                      pulled).
      /// \param allow_missing Do not xxx.
      template <typename Service>
      void
      pull_(memo::cli::Memo& cli,
            Service service,
            boost::optional<std::string> const& account,
            bool allow_missing)
      {
        auto owner = cli.as_user();
        auto where = elle::sprintf("users/%s/credentials/%s", owner.name,
                                   ServicePrint<Service>::name);
        if (account)
          where += elle::sprintf("/%s", *account);
        cli.backend().hub_delete(
          where,
          elle::sprintf("%s credentials", ServicePrint<Service>::pretty),
          ServicePrint<Service>::name, owner, allow_missing);
      }
    }

    void
    Credentials::mode_pull(boost::optional<std::string> const& account,
                           bool aws,
                           bool dropbox,
                           bool gcs,
                           bool google_drive)
    {
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.ensure_at_least_one("pull");
      bool pull_all = e.all();
      auto& cli = this->cli();
      if (e.aws)
      {
        if (pull_all)
          cli.report("INFO: %s credentials are not stored on %s"
                     " (nothing to pull)",
                     AWSPrint::pretty,
                     memo::beyond(true));
        else
          elle::err<CLIError>("%s credentials are not stored on %s",
                              AWSPrint::pretty, memo::beyond(true));
      }
      if (e.dropbox)
        pull_(cli, cli::dropbox, account, false);
      if (e.gcs)
        pull_(cli, cli::gcs, account, false);
      if (e.google_drive)
        pull_(cli, cli::google_drive, account, false);
    }

    /*-------------.
    | Mode: list.  |
    `-------------*/

    namespace
    {
      template <typename Service, typename Fetch>
      void
      list_(memo::Memo& memo,
            Enabled const& enabled,
            Service service,
            Fetch fetch)
      {
        if (service.attr_get(enabled))
        {
          bool first = true;
          for (auto const& credentials: fetch.method_call(memo))
          {
            if (enabled.several() && first)
              std::cout << ServicePrint<Service>::pretty << ":\n";
            if (enabled.several())
              std::cout << "  ";
            std::cout << credentials->uid() << ": "
                      << credentials->display_name() << '\n';
            first = false;
          }
        }
      }
    }

    namespace s
    {
      ELLE_DAS_SYMBOL(credentials_aws);
      ELLE_DAS_SYMBOL(credentials_dropbox);
      ELLE_DAS_SYMBOL(credentials_gcs);
      ELLE_DAS_SYMBOL(credentials_google);
    }

    void
    Credentials::mode_list(bool aws, bool dropbox, bool gcs, bool google_drive)
    {
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.all_if_none();
      auto& memo = this->cli().backend();
      list_(memo, e, cli::aws,          s::credentials_aws);
      list_(memo, e, cli::dropbox,      s::credentials_dropbox);
      list_(memo, e, cli::google_drive, s::credentials_google);
      list_(memo, e, cli::gcs,          s::credentials_gcs);
    }
  }
}
