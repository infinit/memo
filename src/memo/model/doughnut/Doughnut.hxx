#include <elle/Defaulted.hh>

#include <elle/das/named.hh>

namespace memo
{
  using namespace std::literals;
  namespace model
  {
    namespace doughnut
    {
      template <typename T>
      void
      Doughnut::service_add(std::string const& type,
                            std::string const& name,
                            T const& value)
      {
        this->service_add(
          type, name, elle::serialization::binary::serialize(value));
      }

      static auto doughnut_proto =
        elle::das::named::prototype(
            doughnut::id,
            doughnut::keys,
            doughnut::owner,
            doughnut::passport,
            doughnut::consensus_builder,
            doughnut::overlay_builder,
            doughnut::port = boost::optional<int>(),
            doughnut::listen_address = boost::none,
            doughnut::storage = nullptr,
            doughnut::name = boost::optional<std::string>(),
            doughnut::version = boost::optional<elle::Version>(),
            doughnut::admin_keys = AdminKeys(),
            doughnut::rdv_host = boost::optional<std::string>(),
            doughnut::monitoring_socket_path = boost::optional<bfs::path>(),
            doughnut::protocol = Protocol::all,
            doughnut::connect_timeout = elle::defaulted(elle::Duration{5s}),
            doughnut::soft_fail_timeout = elle::defaulted(elle::Duration{20s}),
            doughnut::soft_fail_running = elle::defaulted(false),
            doughnut::tcp_heartbeat = boost::none,
            doughnut::encrypt_options = EncryptOptions(),
            doughnut::resign_on_shutdown = false);

      template <typename ... Args>
      Doughnut::Doughnut(Args&& ... args)
        : Doughnut(doughnut_proto.template map<
                   Address,
                   std::shared_ptr<elle::cryptography::rsa::KeyPair>,
                   std::shared_ptr<elle::cryptography::rsa::PublicKey>,
                   Passport,
                   ConsensusBuilder,
                   OverlayBuilder,
                   boost::optional<int>,
                   boost::optional<boost::asio::ip::address>,
                   std::unique_ptr<silo::Silo>,
                   boost::optional<std::string>,
                   boost::optional<elle::Version>,
                   AdminKeys,
                   boost::optional<std::string>,
                   boost::optional<bfs::path>,
                   Protocol,
                   elle::Defaulted<elle::Duration>,
                   elle::Defaulted<elle::Duration>,
                   elle::Defaulted<bool>,
                   elle::DurationOpt,
                   EncryptOptions,
                   bool>(
                     std::forward<Args>(args)...))
      {}
    }
  }
}

ELLE_DAS_SERIALIZE(memo::model::doughnut::AdminKeys);
ELLE_DAS_SERIALIZE(memo::model::doughnut::EncryptOptions);
