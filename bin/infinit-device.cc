#include <elle/log.hh>

#include <elle/serialization/Serializer.hh>

ELLE_LOG_COMPONENT("infinit-device");

#include <main.hh>
#include <password.hh>

infinit::Infinit ifnt;

static std::string _pair_salt = "5_C+m$:1Ex";

using boost::program_options::variables_map;

std::string
pairing_passphrase(variables_map const& args)
{
  return _password(args, "passphrase", "Passphrase");
}

inline
std::string
get_name(variables_map const& args, std::string const& name = "name")
{
  return get_username(args, name);
}

namespace
{
  /// Add custom headers to be sent to the server.
  ///
  /// \param headers   a map of name -> content.
  template <typename Map>
  void
  headers_add(reactor::http::Request::Configuration& c,
              Map const& headers)
  {
    for (auto const& header: headers)
      c.header_add(header.first, header.second);
  }

  /// Deserialize \a s as a \a T.
  template <typename T>
  auto
  from_json(std::string const& s)
    -> T
  {
    std::stringstream stream;
    stream << s;
    auto input = elle::serialization::json::SerializerIn(stream, false);
    return input.deserialize<T>();
  }

  /// Serialize \a t as Json.
  template <typename T>
  auto
  to_json(T const& t)
    -> std::string
  {
    std::stringstream stream;
    elle::serialization::json::serialize(t, stream, false);
    return stream.str();
  }
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


COMMAND(transmit_user)
{
  auto user = self_user(ifnt, args);
  auto passphrase = pairing_passphrase(args);
  auto key = infinit::cryptography::SecretKey{passphrase};
  auto p = PairingInformation(
    key.encipher(to_json(user),
                 infinit::cryptography::Cipher::aes256),
    hash_password(passphrase, _pair_salt));
  beyond_push(
    elle::sprintf("users/%s/pairing", user.name),
    "user identity for", user.name, p, user, false);
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
          auto c = reactor::http::Request::Configuration{};
          auto headers =
            infinit::signature_headers(reactor::http::Method::GET, where, user);
          headers_add(c, headers);
          auto r = reactor::http::Request(
            elle::sprintf("%s/%s", infinit::beyond(), where),
            reactor::http::Method::GET,
            std::move(c));
          reactor::wait(r);
          switch (r.status())
          {
          case reactor::http::StatusCode::OK:
            // Do nothing.
            break;

          case reactor::http::StatusCode::Not_Found:
            done = true;
            return;

          case reactor::http::StatusCode::Gone:
            timed_out = true;
            return;

          case reactor::http::StatusCode::Forbidden:
            infinit::read_error<infinit::ResourceProtected>(
              r, "user identity", user.name);
            break;

          default:
            elle::err("unexpected HTTP error %s fetching user identity",
                      r.status());
          }
          reactor::sleep(10_sec);
        }
      });
    for (; timeout > 0; timeout--)
    {
      elle::printf("User identity on %s for %s seconds",
                   infinit::beyond(true), timeout);
      std::cout.flush();
      reactor::sleep(1_sec);
      std::cout << '\r' << std::string(80, ' ') << '\r';
      if (done)
      {
        std::cout << "User identity received on another device\n";
        return;
      }
      else if (timed_out)
        break;
    }
    beyond_poller.terminate_now();
    elle::printf("Timed out, user identity no longer available on %s\n",
                 infinit::beyond(true));
  }
}

COMMAND(transmit)
{
  if (flag(args, "user"))
    transmit_user(args, killed);
  else
    throw CommandLineError("Must specify type of object to transmit");
}

COMMAND(receive_user)
{
  auto name = get_name(args);
  auto passphrase = pairing_passphrase(args);
  auto hashed_passphrase = hash_password(passphrase, _pair_salt);
  try
  {
    auto pairing = infinit::beyond_fetch<PairingInformation>(
      elle::sprintf("users/%s/pairing", name), "pairing",
      name, boost::none,
      {{"infinit-pairing-passphrase-hash", hashed_passphrase}},
      false);
    auto key = infinit::cryptography::SecretKey{passphrase};
    auto data = key.decipher(*pairing.data,
                             infinit::cryptography::Cipher::aes256);
    auto user = from_json<infinit::User>(data.string());
    ifnt.user_save(user, true);
  }
  catch (infinit::ResourceGone const& e)
  {
    elle::fprintf(std::cerr,
                  "User identity no longer available on %s, "
                  "retransmit from the original device\n",
                  infinit::beyond(true));
    throw;
  }
  catch (infinit::MissingResource const& e)
  {
    if (e.what() == std::string("user/not_found"))
      not_found(name, "User");
    else if (e.what() == std::string("pairing/not_found"))
      not_found(name, "Pairing");
    throw;
  }
  report_action("received", "user identity for", name);
}

COMMAND(receive)
{
  if (flag(args, "user"))
    receive_user(args, killed);
  else
    throw CommandLineError("Must specify type of object to receive");
}

int
main(int argc, char** argv)
{
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Mode::OptionDescription option_passphrase = {
    "passphrase", value<std::string>(),
    "passphrase to secure identity (default: prompt for passphrase)"
  };
  Modes modes {
    {
      "transmit",
      elle::sprintf("transmit object to another device using %s",
                    infinit::beyond(true)).c_str(),
      &transmit,
      {},
      {
        { "user,u", bool_switch(),
          elle::sprintf("Transmit the user identity to another device using %s",
                        infinit::beyond(true)).c_str(), },
        option_passphrase,
        { "no-countdown", bool_switch(), "do not show countdown timer" },
      },
    },
    {
      "receive",
      elle::sprintf("Receive an object from another device using %s",
                    infinit::beyond(true)).c_str(),
      &receive,
      {},
      {
        { "name,n", value<std::string>(), "name of object to receive" },
        { "user,u", bool_switch(),
          elle::sprintf("receive a user identity from another device using %s",
                        infinit::beyond(true)).c_str() },
        option_passphrase,
      },
    },
  };
  return infinit::main("Infinit device utility", modes, argc, argv);
}
