#include <elle/log.hh>

#include <das/serializer.hh>

ELLE_LOG_COMPONENT("infinit-device");

#include <main.hh>
#include <password.hh>

infinit::Infinit ifnt;

static std::string _pair_salt = "5_C+m$:1Ex";

using namespace boost::program_options;

std::string
pairing_password(variables_map const& args)
{
  return _password(args, "password-inline");
}

inline
std::string
get_name(variables_map const& args, std::string const& name = "name")
{
  return get_username(args, name);
}

struct PairingInformation
{
public:
  // Generating.
  PairingInformation(elle::Buffer const& encrypted_user,
                     std::string const& password)
    : data(encrypted_user)
    , password_hash(password)
  {}
  // Receiving.
  PairingInformation(elle::serialization::SerializerIn& s)
    : data(s.deserialize<elle::Buffer>("data"))
    , password_hash(s.deserialize<std::string>("password_hash"))
  {}

  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("data", this->data);
    s.serialize("password_hash", this->password_hash);
  }

  boost::optional<elle::Buffer> data;
  boost::optional<std::string> password_hash;
};

DAS_MODEL(PairingInformation, (data, password_hash), DasPairingInformation)

COMMAND(transmit_user)
{
  auto user = self_user(ifnt, args);
  auto password = pairing_password(args);
  std::stringstream serialized_user;
  {
    das::Serializer<infinit::DasUser> view{user};
    elle::serialization::json::serialize(view, serialized_user, false);
  }
  infinit::cryptography::SecretKey key{password};
  PairingInformation p(
    key.encipher(serialized_user.str(),
                 infinit::cryptography::Cipher::aes256),
    hash_password(password, _pair_salt));
  das::Serializer<DasPairingInformation> view{p};
  beyond_push(
    elle::sprintf("users/%s/pairing", user.name),
    "transmitting", "user identity", view, user);
}

COMMAND(transmit)
{
  if (flag(args, "user"))
    transmit_user(args);
  else
    throw CommandLineError("Must specify type of object to transmit");
}

COMMAND(receive_user)
{
  auto name = get_name(args);
  auto password = pairing_password(args);
  auto hashed_password = hash_password(password, _pair_salt);
  {
    try
    {
      auto pairing = beyond_fetch<PairingInformation>(
        elle::sprintf("users/%s/pairing", name), "pairing",
        name, boost::none,
        {{"infinit-pairing-password-hash", hashed_password}},
        false);
      infinit::cryptography::SecretKey key{password};
      auto data = key.decipher(*pairing.data,
                               infinit::cryptography::Cipher::aes256);
      std::stringstream stream;
      stream << data.string();
      elle::serialization::json::SerializerIn input(stream, false);
      auto user = input.deserialize<infinit::User>();
      ifnt.user_save(user, true);
    }
    catch (ResourceGone const& e)
    {
      std::cerr << elle::sprintf("User identity fetched and removed from %s",
                                 beyond(true))
                << std::endl;
    }
    catch (MissingResource const& e)
    {
      if (e.what() == std::string("user/not_found"))
        not_found(name, "User");
      if (e.what() == std::string("pairing/not_found"))
        not_found(name, "Pairing");
      throw;
    }
  }
  report_action("saved", "user", name, std::string("locally"));
}

COMMAND(receive)
{
  if (flag(args, "user"))
    receive_user(args);
  else
    throw CommandLineError("Must specify type of object to receive");
}

int
main(int argc, char** argv)
{
  program = argv[0];
  boost::program_options::option_description option_password = {
    "password-inline", value<std::string>(),
    "password to secure identity (default: prompt for password)"
  };
  Modes modes {
    {
      "transmit",
      elle::sprintf("Transmit object to another device using %s", beyond(true)).c_str(),
      &transmit,
      {},
      {
        { "user,u", bool_switch(),
          elle::sprintf("Transmit the user identity to another device using %s",
                        beyond(true)).c_str(), },
        option_password,
        option_owner,
      },
    },
    {
      "receive",
      elle::sprintf("Receive an object from another device using %s",
                    beyond(true)).c_str(),
      &receive,
      {},
      {
        { "name,n", value<std::string>(), "name of object to receive" },
        { "user,u", bool_switch(),
          elle::sprintf("Receive a user identity from another device using %s",
                        beyond(true)).c_str() },
        option_password,
      }
    }
  };
  return infinit::main("Infinit device utility", modes, argc, argv);
}
