#include <elle/log.hh>

#include <das/serializer.hh>

ELLE_LOG_COMPONENT("infinit-device");

#include <main.hh>
#include <password.hh>

infinit::Infinit ifnt;

static std::string _pair_salt = "5_C+m$:1Ex";

using namespace boost::program_options;

std::string
pairing_passphrase(variables_map const& args)
{
  return _password(args, "passphrase");
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
                     std::string const& passphrase)
    : data(encrypted_user)
    , passphrase_hash(passphrase)
  {}
  // Receiving.
  PairingInformation(elle::serialization::SerializerIn& s)
    : data(s.deserialize<elle::Buffer>("data"))
    , passphrase_hash(s.deserialize<std::string>("passphrase_hash"))
  {}

  void
  serialize(elle::serialization::Serializer& s)
  {
    s.serialize("data", this->data);
    s.serialize("passphrase_hash", this->passphrase_hash);
  }

  boost::optional<elle::Buffer> data;
  boost::optional<std::string> passphrase_hash;
};

DAS_MODEL(PairingInformation, (data, passphrase_hash), DasPairingInformation)

COMMAND(transmit_user)
{
  auto user = self_user(ifnt, args);
  auto passphrase = pairing_passphrase(args);
  std::stringstream serialized_user;
  {
    das::Serializer<infinit::DasUser> view{user};
    elle::serialization::json::serialize(view, serialized_user, false);
  }
  infinit::cryptography::SecretKey key{passphrase};
  PairingInformation p(
    key.encipher(serialized_user.str(),
                 infinit::cryptography::Cipher::aes256),
    hash_password(passphrase, _pair_salt));
  das::Serializer<DasPairingInformation> view{p};
  beyond_push(
    elle::sprintf("users/%s/pairing", user.name),
    "user identity for", user.name, view, user, false);
  report_action("transmitted", "user identity for", user.name);
  if (!script_mode && !flag(args, "no-countdown"))
  {
    int timeout = 5 * 60; // 5 min.
    bool timed_out = false;
    bool done = false;
    reactor::Thread beyond_poller(reactor::scheduler(), "beyond poller", [&]
      {
        auto where = elle::sprintf("users/%s/pairing/status", user.name);
        while (timeout > 0)
        {
          reactor::http::Request::Configuration c;
          auto headers =
            signature_headers(reactor::http::Method::GET, where, user);
          for (auto const& header: headers)
            c.header_add(header.first, header.second);
          reactor::http::Request r(
            elle::sprintf("%s/%s", beyond(), where),
            reactor::http::Method::GET,
            std::move(c));
          reactor::wait(r);
          if (r.status() == reactor::http::StatusCode::OK)
          {
            // Do nothing.
          }
          else if (r.status() == reactor::http::StatusCode::Not_Found)
          {
            done = true;
            return;
          }
          else if (r.status() == reactor::http::StatusCode::Gone)
          {
            timed_out = true;
            return;
          }
          else if (r.status() == reactor::http::StatusCode::Forbidden)
          {
            read_error<ResourceProtected>(
              r, "user identity", reactor::http::Method::GET);
          }
          else
          {
            throw elle::Error(
              elle::sprintf("unexpected HTTP error %s fetching user identity",
                            r.status()));
          }
          reactor::sleep(10_sec);
        }
      });
    for (; timeout > 0; timeout--)
    {
      std::cout << elle::sprintf("User identity on %s for: ", beyond(true))
                << timeout << " seconds" << std::flush;
      reactor::sleep(1_sec);
      std::cout << "\r" << std::string(80, ' ') << "\r";
      if (done)
      {
        std::cout << "User identity received on another device" << std::endl;
        return;
      }
      if (timed_out)
        break;
    }
    beyond_poller.terminate_now();
    std::cout << elle::sprintf(
      "Timed out, user identity no longer available on %s", beyond(true))
              << std::endl;
  }
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
  auto passphrase = pairing_passphrase(args);
  auto hashed_passphrase = hash_password(passphrase, _pair_salt);
  {
    try
    {
      auto pairing = beyond_fetch<PairingInformation>(
        elle::sprintf("users/%s/pairing", name), "pairing",
        name, boost::none,
        {{"infinit-pairing-passphrase-hash", hashed_passphrase}},
        false);
      infinit::cryptography::SecretKey key{passphrase};
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
      std::cerr << elle::sprintf("User identity no longer available on %s, "
                                 "retransmit from the original device",
                                 beyond(true))
                << std::endl;
      throw;
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
  report_action("received", "user identity for", name);
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
  boost::program_options::option_description option_passphrase = {
    "passphrase", value<std::string>(),
    "passphrase to secure identity (default: prompt for passphrase)"
  };
  Modes modes {
    {
      "transmit",
      elle::sprintf("transmit object to another device using %s",
                    beyond(true)).c_str(),
      &transmit,
      {},
      {
        { "user,u", bool_switch(),
          elle::sprintf("Transmit the user identity to another device using %s",
                        beyond(true)).c_str(), },
        option_passphrase,
        { "no-countdown", bool_switch(), "do not show countdown timer" },
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
          elle::sprintf("receive a user identity from another device using %s",
                        beyond(true)).c_str() },
        option_passphrase,
      },
    },
  };
  return infinit::main("Infinit device utility", modes, argc, argv);
}
