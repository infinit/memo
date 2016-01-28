#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit-passport");

#include <main.hh>

infinit::Infinit ifnt;

COMMAND(create)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  auto user_name = mandatory(args, "user", "user name");
  auto network = ifnt.network_descriptor_get(network_name, self, true);
  auto user = ifnt.user_get(user_name);
  if (self.public_key != network.owner)
  {
    throw elle::Error(
      elle::sprintf("not owner of network \"%s\"", network_name));
  }
  infinit::model::doughnut::Passport passport(
    user.public_key,
    network.name,
    self.private_key.get());
  if (args.count("output"))
  {
    auto output = get_output(args);
    elle::serialization::json::serialize(passport, *output, false);
    report_action_output(
      *output, "wrote", "passport for", network.name);
  }
  else
  {
    ifnt.passport_save(passport);
    report_created("passport",
                   elle::sprintf("%s: %s", network.name, user_name));
  }
  if (aliased_flag(args, {"push-passport", "push"}))
  {
    beyond_push(
      elle::sprintf("networks/%s/passports/%s", network.name, user.name),
      "passport",
      elle::sprintf("%s: %s", network.name, user.name),
      passport,
      self);
  }
}

COMMAND(export_)
{
  auto self = self_user(ifnt, args);
  auto output = get_output(args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  auto passport = ifnt.passport_get(network_name, user_name);
  {
    elle::serialization::json::serialize(passport, *output, false);
  }
  report_exported(*output, "passport",
                  elle::sprintf("%s: %s", network_name, user_name));
}

COMMAND(fetch)
{
  auto self = self_user(ifnt, args);
  auto network_name = optional(args, "network");
  if (network_name)
    network_name = ifnt.qualified_name(network_name.get(), self);
  auto user_name = optional(args, "user");
  if (network_name && user_name)
  {
    auto passport = beyond_fetch<infinit::Passport>(
      elle::sprintf("networks/%s/passports/%s",
                    network_name.get(), user_name.get()),
      "passport for",
      user_name.get(),
      self);
    ifnt.passport_save(passport);
  }
  // Fetch all network passports if owner else fetch just the user's passport.
  else if (network_name)
  {
    auto owner_name =
      network_name.get().substr(0, network_name.get().find("/"));
    if (owner_name == self.name)
    {
      auto res = beyond_fetch_json(
        elle::sprintf("networks/%s/passports", network_name.get()),
        "passports for",
        network_name.get(),
        self);
      auto json = boost::any_cast<elle::json::Object>(res);
      for (auto const& user_passport: json)
      {
        elle::serialization::json::SerializerIn s(user_passport.second, false);
        auto passport = s.deserialize<infinit::Passport>();
        ifnt.passport_save(passport);
      }
    }
    else
    {
      auto passport = beyond_fetch<infinit::Passport>(elle::sprintf(
        "networks/%s/passports/%s", network_name.get(), self.name),
        "passport for",
        network_name.get(),
        self);
      ifnt.passport_save(passport);
    }
  }
  else if (user_name && user_name.get() != self.name)
  {
    throw CommandLineError("use the --as to fetch passports for another user");
  }
  // Fetch self passports.
  else
  {
    auto res = beyond_fetch<
      std::unordered_map<std::string, std::vector<infinit::Passport>>>(
        elle::sprintf("users/%s/passports", self.name),
        "passports for user",
        self.name,
        self);
    for (auto const& passport: res["passports"])
    {
      try
      {
        ifnt.passport_save(passport);
      }
      catch (ResourceAlreadyFetched const&)
      {}
    }
  }
}

COMMAND(import)
{
  auto input = get_input(args);
  auto passport = elle::serialization::json::deserialize<infinit::Passport>
    (*input, false);
  ifnt.passport_save(passport);
  std::string user_name;
  for (auto const& user: ifnt.users_get())
  {
    if (user.public_key == passport.user())
    {
      user_name = user.name;
      break;
    }
  }
  report_imported("passport",
                  elle::sprintf("%s: %s", passport.network(), user_name));
}

COMMAND(push)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  auto passport = ifnt.passport_get(network_name, user_name);
  {
    beyond_push(
      elle::sprintf("networks/%s/passports/%s", network_name, user_name),
      "passport",
      elle::sprintf("%s: %s", network_name, user_name),
      passport,
      self);
  }
}

COMMAND(pull)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  {
    beyond_delete(
      elle::sprintf("networks/%s/passports/%s", network_name, user_name),
      "passport for",
      user_name,
      self);
  }
}

COMMAND(list)
{
  namespace boost_fs = boost::filesystem;
  auto network_name = optional(args, "network");
  boost_fs::path path;
  if (network_name)
  {
    if (!ifnt.is_qualified_name(network_name.get()))
    {
      auto self = self_user(ifnt, args);
      network_name = ifnt.qualified_name(network_name.get(), self);
    }
    path = ifnt._passport_path() / network_name.get();
  }
  else
  {
    path = ifnt._passport_path();
  }
  for (boost_fs::recursive_directory_iterator it(path);
       it != boost_fs::recursive_directory_iterator();
       ++it)
  {
    if (is_regular_file(it->status()) && !is_hidden_file(it->path()))
    {
      auto user_name = it->path().filename().string();
      boost_fs::ifstream f;
      ifnt._open_read(f, it->path(), user_name, "passport");
      elle::serialization::json::SerializerIn s(f, false);
      auto passport =  s.deserialize<infinit::Passport>();
      std::cout << passport.network() << ": " << user_name << std::endl;
    }
  }
}

COMMAND(delete_)
{
  auto self = self_user(ifnt, args);
  auto network_name = mandatory(args, "network", "network name");
  network_name = ifnt.qualified_name(network_name, self);
  auto user_name = mandatory(args, "user", "user name");
  auto path = ifnt._passport_path(network_name, user_name);
  if (!boost::filesystem::exists(path))
    throw elle::Error(
      elle::sprintf("Passport for %s in %s not found", user_name, network_name));
  if (boost::filesystem::remove(path))
  {
    report_action("deleted", "passport",
                  elle::sprintf("%s: %s", network_name, user_name),
                  std::string("locally"));
  }
  else
  {
    throw elle::Error(
      elle::sprintf("File for passport could not be deleted: %s", path));
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "create",
      "Create a passport for a user to a network",
      &create,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(),
          "network to create the passport to" },
        { "user,u", value<std::string>(), "user to create the passport for" },
        { "push-passport", bool_switch(),
          elle::sprintf("push the passport to %s", beyond(true)).c_str() },
        { "push,p", bool_switch(), "alias for --push-passport" },
        option_output("passport"),
      },
    },
    {
      "export",
      "Export a user's network passport",
      &export_,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network to export passport for" },
        { "user,u", value<std::string>(), "user to export passport for" },
        option_output("passport"),
      },
    },
    {
      "fetch",
      elle::sprintf("Fetch a user's network passport from %s",
                    beyond(true)).c_str(),
      &fetch,
      "[--network NETWORK --user USER]",
      {
        { "network,n", value<std::string>(),
          "network to fetch the passport for (optional)" },
        { "user,u", value<std::string>(),
          "user to fetch passports for (optional)" },
      },
    },
    {
      "import",
      "Import a passport for a user to a network",
      &import,
      "[--input INPUT]",
      {
        option_input("passport"),
      },
    },
    {
      "push",
      elle::sprintf("Push a user's network passport to %s",
                    beyond(true)).c_str(),
      &push,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network name" },
        { "user,u", value<std::string>(), "user name" },
      },
    },
    {
      "pull",
      elle::sprintf("Remove a user's network passport from %s",
                    beyond(true)).c_str(),
      &pull,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network name" },
        { "user,u", value<std::string>(), "user name" },
      },
    },
    {
      "list",
      "List all local passports",
      &list,
      "[--network NETWORK]",
      {
        { "network,n", value<std::string>(),
          "network to list passports for (optional)" },
      },
    },
    {
      "delete",
      "Locally delete a passport",
      &delete_,
      "--network NETWORK --user USER",
      {
        { "network,n", value<std::string>(), "network name" },
        { "user,u", value<std::string>(), "user name" },
      },
    },
  };
  return infinit::main("Infinit volume management utility", modes, argc, argv);
}
