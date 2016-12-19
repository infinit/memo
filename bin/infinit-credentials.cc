#include <elle/err.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh>

ELLE_LOG_COMPONENT("infinit-credentials");

#include <main.hh>

infinit::Infinit ifnt;

static const boost::regex _aws_access_key_regex("[A-Z0-9]{20}");
static const boost::regex _aws_secrect_key_regex("[A-Za-z0-9/+=]{40}");

static
std::string
read_key(std::string const& prompt_text, boost::regex const& regex)
{
  boost::smatch matches;
  std::string res;
  {
    bool first = true;
    while (!boost::regex_match(res, matches, regex))
    {
      if (!first)
      {
        std::cout << "Invalid \"" << prompt_text << "\", try again..."
                  << std::endl;
      }
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
  }
  return res;
}

COMMAND(add)
{
  auto user = self_user(ifnt, args);
  if (args.count("dropbox"))
  {
    std::cout << "Register your Dropbox account with infinit by visiting "
              << infinit::beyond() << "/users/" << user.name
              << "/dropbox-oauth" << std::endl;
  }
  else if (args.count("google"))
  {
    std::cout << "Register your Google account with infinit by visiting "
              << infinit::beyond() << "/users/" << user.name
              << "/google-oauth" << std::endl;
  }
  else if (args.count("gcs"))
  {
    std::cout << "Register your Google account with infinit by visiting "
              << infinit::beyond() << "/users/" << user.name
              << "/gcs-oauth" << std::endl;
  }
  else if (args.count("aws"))
  {
    auto account = mandatory(args, "name", "account name");
    std::cout << "Please enter your AWS credentials" << std::endl;
    std::string access_key_id =
      read_key("Access Key ID", _aws_access_key_regex);
    std::string secret_access_key =
      read_key("Secret Access Key", _aws_secrect_key_regex);
    auto aws_credentials = std::make_unique<infinit::AWSCredentials>(
      account, access_key_id, secret_access_key);
    ifnt.credentials_aws_add(std::move(aws_credentials));
    report_action("stored", "AWS credentials", account, "locally");
  }
  else
    throw CommandLineError("service type not specified");
}

struct Enabled
{
  Enabled(bool aws_, bool dropbox_, bool gcs_, bool google_)
    : aws(aws_)
    , dropbox(dropbox_)
    , gcs(gcs_)
    , google(google_)
  {
    // None is all.
    if (!aws && !dropbox && !gcs && !google)
      aws = dropbox = gcs = google = true;
  }

  Enabled(boost::program_options::variables_map const& args)
    : Enabled(bool(args.count("aws")),
              bool(args.count("dropbox")),
              bool(args.count("gcs")),
              bool(args.count("google")))
  {}

  bool multi() const
  {
    return 1 < aws + dropbox + gcs + google;
  }

  bool all() const
  {
    return aws && dropbox && gcs && google;
  }

  bool aws;
  bool dropbox;
  bool gcs;
  bool google;
};


template <typename T>
void
fetch_credentials(infinit::User const& user,
                  std::string const& name,
                  std::string const& pretty,
                  std::function<void (std::unique_ptr<T>)> add)
{
  std::string where = elle::sprintf("users/%s/credentials/%s", user.name, name);
  // FIXME: Workaround for using std::unique_ptr.
  // Remove when serialization does not require copy.
  auto res = beyond_fetch_json(
    where, elle::sprintf("\"%s\" credentials for", pretty), user.name, user,
    infinit::Headers{}, false);
  auto root = boost::any_cast<elle::json::Object>(res);
  auto credentials_vec =
      boost::any_cast<std::vector<elle::json::Json>>(root["credentials"]);
  for (auto const& a_json: credentials_vec)
  {
    elle::serialization::json::SerializerIn input(a_json, false);
    auto a = std::make_unique<T>(input.deserialize<T>());
    elle::printf("Fetched %s credentials %s (%s)\n",
                 pretty, a->uid(), a->display_name());
    add(std::move(a));
  }
}

COMMAND(fetch)
{
  auto e = Enabled(args);
  bool fetch_all = e.all();
  if (e.aws && !fetch_all)
  {
    throw CommandLineError(elle::sprintf("AWS credentials are not stored on %s",
                                         infinit::beyond(true)));
  }
  auto user = self_user(ifnt, args);
  if (e.dropbox)
    fetch_credentials<infinit::OAuthCredentials>(
      user, "dropbox", "Dropbox",
      [] (std::unique_ptr<infinit::OAuthCredentials> a)
      { ifnt.credentials_dropbox_add(std::move(a)); });
  if (e.gcs)
    fetch_credentials<infinit::OAuthCredentials>(
      user, "gcs", "Google Cloud Storage",
      [] (std::unique_ptr<infinit::OAuthCredentials> a)
      { ifnt.credentials_gcs_add(std::move(a)); });
  if (e.google)
    fetch_credentials<infinit::OAuthCredentials>(
      user, "google", "Google Drive",
      [] (std::unique_ptr<infinit::OAuthCredentials> a)
      { ifnt.credentials_google_add(std::move(a)); });
  if (!script_mode && fetch_all)
  {
    std::cout << "INFO: AWS credentials are not stored on " << infinit::beyond(true)
              << " and so were not fetched" << std::endl;
  }
  // FIXME: remove deleted ones
}

/// \param multi   whether there are several services to list.
/// \param fetch   function that returns the credentials.
/// \param service_name  The displayed name.
template <typename Service, typename Fetch>
void
list_(Enabled const& e,
      Service service,
      Fetch fetch,
      std::string const& service_name)
{
  if (!service.attr_get(e))
    return;
  bool first = true;
  for (auto const& credentials: fetch.method_call(ifnt))
  {
    if (e.multi() && first)
      std::cout << service_name << ":\n";
    if (e.multi())
      std::cout << "  ";
    std::cout << credentials->uid() << ": "
              << credentials->display_name() << '\n';
    first = false;
  }
}

namespace s
{
  DAS_SYMBOL(aws);
  DAS_SYMBOL(credentials_aws);
  DAS_SYMBOL(dropbox);
  DAS_SYMBOL(credentials_dropbox);
  DAS_SYMBOL(gcs);
  DAS_SYMBOL(credentials_gcs);
  DAS_SYMBOL(google);
  DAS_SYMBOL(credentials_google);
}

COMMAND(list)
{
  auto e = Enabled(args);
  list_(e, s::aws, s::credentials_aws, "AWS");
  list_(e, s::dropbox, s::credentials_dropbox, "Dropbox");
  list_(e, s::google, s::credentials_google, "Google");
  list_(e, s::gcs, s::credentials_gcs, "GCS");
}

COMMAND(delete_)
{
  auto account = mandatory(args, "name", "account name");
  std::string service = "";
  if (args.count("aws"))
    service = "aws";
  else if (args.count("dropbox"))
    service = "dropbox";
  else if (args.count("gcs"))
    service = "gcs";
  else if (args.count("google"))
    service = "google";
  if (service.empty())
    throw CommandLineError("specify a service");
  auto path = ifnt._credentials_path(service, account);
  if (boost::filesystem::remove(path))
    report_action("deleted", "credentials", account, "locally");
  else
    elle::err("File for credentials could not be deleted: %s", path);
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  Mode::OptionsDescription services_options("Services");
  services_options.add_options()
    ("aws", "Amazon Web Services (or S3 compatible) credentials")
    ("gcs", "Google Cloud Storage credentials")
    ;
  Mode::OptionsDescription hidden_service_options("Hidden credential types");
  hidden_service_options.add_options()
    ("dropbox", "Dropbox account credentials")
    ("google", "Google account credentials")
    ;
  Mode::OptionsDescription aws_options("AWS account options");
  aws_options.add_options()
    ("name", value<std::string>(), "account name")
    ;
  Modes modes {
    {
      "add",
      "Add credentials for a third-party service",
      &add,
      "SERVICE",
      {},
      {services_options, aws_options},
      {},
      {hidden_service_options},
    },
    {
      "fetch",
      elle::sprintf("Fetch credentials from %s", infinit::beyond(true)),
      &fetch,
      "[SERVICE]",
      {},
      {services_options},
      {},
      {hidden_service_options},
    },
    {
      "list",
      "List local credentials",
      &list,
      "[SERVICE]",
      {},
      {services_options},
      {},
      {hidden_service_options},
    },
    {
      "delete",
      "Delete credentials locally",
      &delete_,
      "SERVICE --name NAME",
      {
        { "name,n", value<std::string>(), "account name" },
      },
      {services_options},
    },
  };
  return infinit::main("Infinit third-party credentials utility",
                       modes, argc, argv);
}
