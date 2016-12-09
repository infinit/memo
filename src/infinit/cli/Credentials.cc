#include <infinit/cli/Credentials.hh>

#include <iostream>

#include <infinit/cli/Infinit.hh>

ELLE_LOG_COMPONENT("infinit-credentials");

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
      , list(
        "List local credentials",
        das::cli::Options(),
        this->bind(modes::mode_list,
                   aws = false,
                   dropbox = false,
                   gcs = false,
                   google_drive = false))
    {}


    /*------.
    | Modes |
    `------*/

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

    static
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

    void
    Credentials::mode_add(boost::optional<std::string> account,
                          bool aws,
                          bool dropbox,
                          bool gcs,
                          bool google_drive)
    {
      static auto const mandatory =
        [] (auto o, std::string const& name)
        {
          if (o)
            return o.get();
          else
            throw das::cli::MissingOption(name);
        };
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

#if 0
    void
    Credentials::mode_delete(std::string const& name,
                             bool pull,
                             bool purge)
    {
    }

    void
    Credentials::mode_fetch(std::vector<std::string> const& user_names,
                            bool no_avatar)
    {
    }
#endif

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

      bool multi() const
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

    /// \param multi   whether there are several services to list.
    /// \param fetch   function that returns the credentials.
    /// \param service_name  the displayed name.
    template <typename Service, typename Fetch>
    void
    list_(Enabled const& enabled,
          infinit::Infinit& ifnt,
          Service service,
          Fetch fetch,
          std::string const& service_name)
    {
      if (service.attr_get(enabled))
      {
        bool first = true;
        for (auto const& credentials: fetch.method_call(ifnt))
        {
          if (enabled.multi() && first)
            std::cout << service_name << ":\n";
          if (enabled.multi())
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
      list_(e, ifnt, cli::aws,          s::credentials_aws, "AWS");
      list_(e, ifnt, cli::dropbox,      s::credentials_dropbox, "Dropbox");
      list_(e, ifnt, cli::google_drive, s::credentials_google, "Google");
      list_(e, ifnt, cli::gcs,          s::credentials_gcs, "GCS");
    }
  }
}
