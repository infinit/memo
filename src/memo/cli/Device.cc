#include <memo/cli/Device.hh>

#include <elle/serialization/Serializer.hh>

#include <memo/cli/Memo.hh>
#include <memo/model/doughnut/consensus/Paxos.hh>

ELLE_LOG_COMPONENT("cli.device");

namespace memo
{
  namespace cli
  {
    namespace
    {
      std::string _pair_salt = "5_C+m$:1Ex";

      /// Add custom headers to be sent to the server.
      ///
      /// \param headers   a map of name -> content.
      template <typename Map>
      void
      headers_add(elle::reactor::http::Request::Configuration& c,
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

      void
      not_found(std::string const& name,
                std::string const& type)
      {
        elle::fprintf(std::cerr,
                      "%s %s not found on %s, ensure it has been pushed\n",
                      type, name, memo::beyond(true));
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


    /*---------.
    | Device.  |
    `---------*/

    Device::Device(Memo& memo)
      : Object(memo)
      , receive(*this,
                "Receive an object from another device using {hub}",
                elle::das::cli::Options{
                  {"user", {'u', "{action} user identity from "
                                 "another device using {hub}", false}}
                },
                cli::user = false,
                cli::name = memo.default_user_name(),
                cli::passphrase = boost::none)
      , transmit(*this,
                 "transmit an object to another device using {hub}",
                 elle::das::cli::Options{
                   {"user", {'u', "{action} user identity to "
                                  "another device using {hub}", false}}
                 },
                 cli::user = false,
                 cli::passphrase = boost::none,
                 cli::no_countdown = false)
    {}

    /*----------------.
    | Mode: receive.  |
    `----------------*/

    namespace
    {
      void
      receive_user(cli::Memo& cli,
                   std::string const& name,
                   boost::optional<std::string> const& passphrase)
      {
        auto& memo = cli.memo();
        auto pass = passphrase ? *passphrase : Memo::read_passphrase();
        auto hashed_pass = cli.hash_password(pass, _pair_salt);
        try
        {
          auto pairing = memo.hub_fetch<PairingInformation>(
            elle::sprintf("users/%s/pairing", name), "pairing",
            name, boost::none,
            {{"infinit-pairing-passphrase-hash", hashed_pass}});
          auto key = elle::cryptography::SecretKey{pass};
          auto data = key.decipher(*pairing.data,
                                   elle::cryptography::Cipher::aes256);
          auto user = from_json<memo::User>(data.string());
          memo.user_save(user, true);
        }
        catch (memo::ResourceGone const& e)
        {
          elle::fprintf(std::cerr,
                        "User identity no longer available on %s, "
                        "retransmit from the original device\n",
                        memo::beyond(true));
          throw;
        }
        catch (memo::MissingResource const& e)
        {
          if (e.what() == std::string("user/not_found"))
            not_found(name, "User");
          else if (e.what() == std::string("pairing/not_found"))
            not_found(name, "Pairing");
          throw;
        }
        cli.report_action("received", "user identity for", name);
      }
    }

    void
    Device::mode_receive(bool user,
                         std::string const& name,
                         boost::optional<std::string> const& passphrase)
    {
      if (user)
        receive_user(this->cli(), name, passphrase);
      else
        elle::err<CLIError>("Must specify type of object to receive");
    }


    /*-----------------.
    | Mode: transmit.  |
    `-----------------*/

    namespace
    {
      void
      transmit_user(cli::Memo& cli,
                    boost::optional<std::string> const& passphrase,
                    bool countdown)
      {
        auto& memo = cli.memo();
        auto user = cli.as_user();
        auto pass = passphrase ? *passphrase : Memo::read_passphrase();
        auto key = elle::cryptography::SecretKey{pass};
        auto p = PairingInformation(
          key.encipher(to_json(user),
                       elle::cryptography::Cipher::aes256),
          cli.hash_password(pass, _pair_salt));
        memo.hub_push
          (elle::sprintf("users/%s/pairing", user.name),
           "user identity for", user.name, p, user, false);
        cli.report_action("transmitted", "user identity for", user.name);
        if (!cli.script() && countdown)
        {
          int timeout = 5 * 60; // 5 min.
          bool timed_out = false;
          bool done = false;
          auto&& hub_poller =
            elle::reactor::Thread(elle::reactor::scheduler(), "beyond poller", [&]
            {
              namespace http = elle::reactor::http;
              auto where = elle::sprintf("users/%s/pairing/status", user.name);
              while (timeout > 0)
              {
                auto c = http::Request::Configuration{};
                auto headers =
                  memo::signature_headers(http::Method::GET, where, user);
                headers_add(c, headers);
                auto r = http::Request
                  (elle::sprintf("%s/%s", memo::beyond(), where),
                   http::Method::GET,
                   std::move(c));
                elle::reactor::wait(r);
                switch (r.status())
                {
                case http::StatusCode::OK:
                  // Do nothing.
                  break;

                case http::StatusCode::Not_Found:
                  done = true;
                  return;

                case http::StatusCode::Gone:
                  timed_out = true;
                  return;

                case http::StatusCode::Forbidden:
                  memo::read_error<memo::ResourceProtected>
                    (r, "user identity", user.name);
                  break;

                default:
                  elle::err("unexpected HTTP error %s fetching user identity",
                            r.status());
                }
                elle::reactor::sleep(10_sec);
              }
            });
          for (; timeout > 0; timeout--)
          {
            elle::fprintf(std::cout, "User identity on %s for %s seconds",
                          memo::beyond(true), timeout);
            std::cout.flush();
            elle::reactor::sleep(1_sec);
            std::cout << '\r' << std::string(80, ' ') << '\r';
            if (done)
            {
              std::cout << "User identity received on another device\n";
              return;
            }
            else if (timed_out)
              break;
          }
          hub_poller.terminate_now();
          elle::fprintf(
            std::cout, "Timed out, user identity no longer available on %s\n",
            memo::beyond(true));
        }
      }
    }

    void
    Device::mode_transmit(bool user,
                          boost::optional<std::string> const& passphrase,
                          bool no_countdown)
    {
      if (user)
        transmit_user(this->cli(), passphrase, !no_countdown);
      else
        elle::err<CLIError>("Must specify type of object to receive");
    }
  }
}
