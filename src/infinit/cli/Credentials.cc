#include <infinit/cli/Credentials.hh>

#include <iostream>

#include <elle/err.hh>

#include <infinit/cli/Infinit.hh>

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
                   account = boost::none,
                   aws = false,
                   dropbox = false,
                   gcs = false,
                   google_drive = false))
      , delete_(
        "Delete locally credentials for a third-party service",
        das::cli::Options(),
        this->bind(modes::mode_delete,
                   account = boost::none,
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
        bool all() const
        {
          return aws && dropbox && gcs && google_drive;
        }

        bool none() const
        {
          return !aws && !dropbox && !gcs && !google_drive;
        }

        bool several() const
        {
          return 1 < aws + dropbox + gcs + google_drive;
        }

        void all_if_none()
        {
          if (none())
            aws = dropbox = gcs = google_drive = true;
        }

        bool aws;
        bool dropbox;
        bool gcs;
        bool google_drive;
      };

      template <typename Symbol>
      auto mandatory(Symbol const& o, std::string const& name)
      {
        if (o)
          return o.get();
        else
          throw das::cli::MissingOption(name);
      };
    }

    /*----------.
    | Mode: add |
    `----------*/

    /// Point to the web documentation to register a user on a given
    /// service provider.
    ///
    /// \param account  user name
    /// \param service  service name in clear (e.g., "Google Drive")
    /// \param page     name used on our web page (e.g., "google")
    static
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

    namespace
    {
      std::string
      read_key(std::string const& prompt_text, boost::regex const& regex)
      {
        auto res = std::string{};
        bool first = true;
        boost::smatch matches;
        while (!boost::regex_match(res, matches, regex))
        {
          if (!first)
            std::cout << "Invalid \"" << prompt_text << "\", try again...\n";
          else
            first = false;
          std::cout << prompt_text << ": ";
          std::cout.flush();
          if (!std::getline(std::cin, res))
            {
              reactor::yield();
              elle::err("Aborting...");
            }
          std::cin.clear();
        }
        return res;
      }
    }

    void
    Credentials::mode_add(boost::optional<std::string> account,
                          bool aws,
                          bool dropbox,
                          bool gcs,
                          bool google_drive)
    {
      auto account_name = mandatory(account, "account");
      auto& ifnt = this->cli().infinit();
      if (aws)
      {
        static auto const access_key_re = boost::regex("[A-Z0-9]{20}");
        static auto const secret_key_re = boost::regex("[A-Za-z0-9/+=]{40}");

        std::cout << "Please enter your AWS credentials\n";
        std::string access_key_id = read_key("Access Key ID",
                                             access_key_re);
        std::string secret_access_key = read_key("Secret Access Key",
                                                 secret_key_re);
        auto aws_credentials =
          std::make_unique<infinit::AWSCredentials>(account_name,
                                                    access_key_id,
                                                    secret_access_key);
        ifnt.credentials_aws_add(std::move(aws_credentials));
        this->cli().report_action("stored", "AWS credentials",
                                  account.get(), std::string("locally"));
      }
      else if (dropbox)
        web_doc(account_name, "Dropbox", "dropbox");
      else if (gcs)
        web_doc(account_name, "Google", "gcs");
      else if (google_drive)
        web_doc(account_name, "Google", "google");
      else
        elle::err<Error>("service type not specified");
    }

    /*---------------.
    | Mode: delete.  |
    `---------------*/

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
          cred.cli().report_action("deleted", "credentials",
                                   account_name, std::string("locally"));
        else
          elle::err("File for credentials could not be deleted: %s", path);
      }
    }

    void
    Credentials::mode_delete(boost::optional<std::string> account,
                             bool aws,
                             bool dropbox,
                             bool gcs,
                             bool google_drive)
    {
      auto e = Enabled{aws, dropbox, gcs, google_drive};
      if (e.none())
        elle::err("delete: specify a service");
      auto a = mandatory(account, "account");
      do_delete_(*this, e, cli::aws,          "aws", a);
      do_delete_(*this, e, cli::dropbox,      "dropbox", a);
      do_delete_(*this, e, cli::google_drive, "google", a);
      do_delete_(*this, e, cli::gcs,          "gcs", a);
    }


    /*-------------.
    | Mode: list.  |
    `-------------*/

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
