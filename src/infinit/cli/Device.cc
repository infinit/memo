#include <infinit/cli/Device.hh>

#include <elle/serialization/Serializer.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>

namespace infinit
{
  namespace cli
  {
    using Error = das::cli::Error;

    namespace
    {
      std::string _pair_salt = "5_C+m$:1Ex";

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
    }

    Device::Device(Infinit& infinit)
      : Entity(infinit)
      , transmit(
        elle::sprintf("transmit an object to another device using %s",
                      infinit::beyond(true)),
        das::cli::Options(),
        this->bind(modes::mode_transmit,
                   cli::user = false,
                   cli::name = std::string{},
                   cli::passphrase = boost::none,
                   cli::no_countdown = false))
    {}

    /*-----------------.
    | Mode: transmit.  |
    `-----------------*/

    namespace
    {
      void
      transmit_user(cli::Infinit& cli,
                    std::string const& name,
                    boost::optional<std::string> const& passphrase,
                    bool countdown)
      {
        auto& ifnt = cli.infinit();
        auto user = ifnt.user_get(name);
        auto pass = passphrase.value_or(Infinit::read_passphrase());
        auto key = infinit::cryptography::SecretKey{pass};
        auto p = PairingInformation(
          key.encipher(to_json(user),
                       infinit::cryptography::Cipher::aes256),
          cli.hash_password(pass, _pair_salt));
        ifnt.beyond_push
          (elle::sprintf("users/%s/pairing", user.name),
           "user identity for", user.name, p, user, false);
        cli.report_action("transmitted", "user identity for", user.name);
        if (!cli.script() && countdown)
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
                auto r = reactor::http::Request
                  (elle::sprintf("%s/%s", infinit::beyond(), where),
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
                  infinit::read_error<infinit::ResourceProtected>
                    (r, "user identity", user.name);
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
    }

    void
    Device::mode_transmit(bool user,
                          boost::optional<std::string> const& name,
                          boost::optional<std::string> const& passphrase,
                          bool no_countdown)
    {
      if (user)
        transmit_user(this->cli(), name.value(), passphrase, !no_countdown);
      else
        elle::err<Error>("Must specify type of object to receive");
    }
  }
}
