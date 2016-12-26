#include <infinit/cli/Credentials.hh>

#include <iostream>

#include <elle/err.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/cli/utility.hh>

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    Credentials::Credentials(Infinit& infinit)
      : Entity(infinit)
      , add(
        "Add credentials for a third-party service",
        das::cli::Options(),
        this->bind(modes::mode_add,
                   name = boost::none,
                   aws = false,
                   dropbox = false,
                   gcs = false,
                   google_drive = false))
      , delete_(
        "Delete locally credentials for a third-party service",
        das::cli::Options(),
        this->bind(modes::mode_delete,
                   name,
                   aws = false,
                   dropbox = false,
                   gcs = false,
                   google_drive = false))
      , fetch(
        "Fetch credentials from {hub}",
        das::cli::Options(),
        this->bind(modes::mode_fetch,
                   name = boost::none,
                   aws = false,
                   dropbox = false,
                   gcs = false,
                   google_drive = false))
      , list(
        "List local credentials",
        das::cli::Options(),
        this->bind(modes::mode_list,
                   aws = false,
                   dropbox = false,
                   gcs = false,
                   google_drive = false))
    {}

    namespace
    {
      /// A structure easy to query using das symbols.
      ///
      /// E.g., `google.attr_get(enabled)` -> true/false, where
      /// `google` is a das::Symbol.
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
      /// \param account  user name
      /// \param service  service name in clear (e.g., "Google Drive")
      /// \param page     name used on our web page (e.g., "google")
      void
      web_doc(std::string const& account,
              std::string const& service, std::string const& page)
      {
        std::cout << "Register your "
                  << service
                  << " account with infinit by visiting "
                  << infinit::beyond() << "/users/" << account
                  << "/"
                  << page
                  << "-oauth" << '\n';
      }
    }

    void
    Credentials::mode_add(boost::optional<std::string> const& account,
                          bool aws,
                          bool dropbox,
                          bool gcs,
                          bool google_drive)
    {
      auto& cli = this->cli();
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.ensure_at_least_one("add");
      if (aws)
      {
        auto account_name = mandatory(account, "account");
        std::cout << "Please enter your AWS credentials\n";
        auto access_key_id
          = this->cli().read_secret("Access Key ID", "[A-Z0-9]{20}");
        auto secret_access_key
          = this->cli().read_secret("Secret Access Key", "[A-Za-z0-9/+=]{40}");
        auto aws_credentials =
          std::make_unique<infinit::AWSCredentials>(account_name,
                                                    access_key_id,
                                                    secret_access_key);
        ifnt.credentials_aws_add(std::move(aws_credentials));
        this->cli().report_action("stored", "AWS credentials",
                                  account.get(), "locally");
      }
      else if (dropbox)
        web_doc(owner.name, "Dropbox", "dropbox");
      else if (gcs)
        web_doc(owner.name, "Google", "gcs");
      else if (google_drive)
        web_doc(owner.name, "Google", "google");
    }

    /*---------------.
    | Mode: delete.  |
    `---------------*/

    namespace
    {
      template <typename Service>
      void
      do_delete_(Credentials& cred,
                 Enabled const& enabled,
                 Service service,
                 std::string const& service_name,
                 std::string const& account_name)
      {
        if (service.attr_get(enabled))
        {
          auto& ifnt = cred.cli().infinit();
          auto path = ifnt._credentials_path(service_name, account_name);
          if (boost::filesystem::remove(path))
            cred.cli().report_action(
              "deleted", elle::sprintf("%s credentials", service_name),
              account_name, "locally");
          else
            elle::err("File for credentials could not be deleted: %s", path);
        }
      }
    }

    void
    Credentials::mode_delete(std::string const& account,
                             bool aws,
                             bool dropbox,
                             bool gcs,
                             bool google_drive)
    {
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.ensure_at_least_one("delete");
      do_delete_(*this, e, cli::aws,          "aws",     account);
      do_delete_(*this, e, cli::dropbox,      "dropbox", account);
      do_delete_(*this, e, cli::google_drive, "google",  account);
      do_delete_(*this, e, cli::gcs,          "gcs",     account);
    }


    /*--------------.
    | Mode: fetch.  |
    `--------------*/

    namespace
    {
      using Cred = infinit::OAuthCredentials;
      using UCred = std::unique_ptr<Cred>;

      /// Fetch credentials.
      ///
      /// \param user     the user
      /// \param service  name of the service (e.g., "google_drive").
      /// \param pretty   pretty name of the service (e.g., "Google Drive")
      /// \param add      function to call to add the fetched credentials
      /// \param account  name of a specific credentials to fetch
      void
      fetch_(infinit::User const& user,
             std::string const& service,
             std::string const& pretty,
             std::function<void (UCred)> add,
             boost::optional<std::string> const& account)
      {
        auto where = elle::sprintf("users/%s/credentials/%s", user.name,
                                   service);
        if (account)
          where += elle::sprintf("/%s", *account);
        // FIXME: Workaround for using std::unique_ptr.
        // Remove when serialization does not require copy.
        auto res =
          infinit::Infinit::beyond_fetch_json
          (where,
           elle::sprintf("\"%s\" credentials for", pretty),
           user.name, user);
        auto root = boost::any_cast<elle::json::Object>(res);
        auto credentials_vec =
          boost::any_cast<std::vector<elle::json::Json>>(root["credentials"]);
        for (auto const& a_json: credentials_vec)
        {
          auto input = elle::serialization::json::SerializerIn(a_json, false);
          auto a = elle::make_unique<Cred>(input.deserialize<Cred>());
          elle::printf("Fetched %s credentials %s (%s)\n",
                       pretty, a->uid(), a->display_name());
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
      auto& ifnt = cli.infinit();
      auto owner = cli.as_user();
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.all_if_none();
      bool fetch_all = e.all();
      // FIXME: Use Symbols instead.
      if (e.aws)
      {
        if (fetch_all)
          this->cli().report("INFO: AWS credentials are not stored on %s"
                             " and so were not fetched",
                             infinit::beyond(true));
        else
          elle::err<Error>("AWS credentials are not stored on %s",
                           infinit::beyond(true));
      }
      if (e.dropbox)
        fetch_
          (owner, "dropbox", "Dropbox",
           [&ifnt] (UCred a)
           { ifnt.credentials_dropbox_add(std::move(a)); },
           account);
      if (e.gcs)
        fetch_
          (owner, "gcs", "Google Cloud Storage",
           [&ifnt] (UCred a)
           { ifnt.credentials_gcs_add(std::move(a)); },
           account);
      if (e.google_drive)
        fetch_
          (owner, "google", "Google Drive",
           [&ifnt] (UCred a)
           { ifnt.credentials_google_add(std::move(a)); },
           account);
      // FIXME: remove deleted ones
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/

    namespace
    {
      template <typename Service, typename Fetch>
      void
      list_(infinit::Infinit& ifnt,
            Enabled const& enabled,
            Service service,
            Fetch fetch,
            std::string const& service_name)
      {
        if (service.attr_get(enabled))
        {
          bool first = true;
          for (auto const& credentials: fetch.method_call(ifnt))
          {
            if (enabled.several() && first)
              std::cout << service_name << ":\n";
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
      DAS_SYMBOL(credentials_aws);
      DAS_SYMBOL(credentials_dropbox);
      DAS_SYMBOL(credentials_gcs);
      DAS_SYMBOL(credentials_google);
    }

    void
    Credentials::mode_list(bool aws, bool dropbox, bool gcs, bool google_drive)
    {
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      e.all_if_none();
      auto& ifnt = this->cli().infinit();
      list_(ifnt, e, cli::aws,          s::credentials_aws, "AWS");
      list_(ifnt, e, cli::dropbox,      s::credentials_dropbox, "Dropbox");
      list_(ifnt, e, cli::google_drive, s::credentials_google, "Google");
      list_(ifnt, e, cli::gcs,          s::credentials_gcs, "GCS");
    }
  }
}
