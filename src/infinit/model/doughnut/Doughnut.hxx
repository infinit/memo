#include <elle/Defaulted.hh>

#include <elle/das/named.hh>

namespace infinit
{
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
                     doughnut::monitoring_socket_path =
                     boost::optional<boost::filesystem::path>(),
                     doughnut::protocol = Protocol::all,
                     doughnut::connect_timeout =
                     elle::defaulted(std::chrono::milliseconds(5000)),
                     doughnut::soft_fail_timeout =
                     elle::defaulted(std::chrono::milliseconds(20000)),
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
                   boost::optional<boost::filesystem::path>,
                   Protocol,
                   elle::Defaulted<std::chrono::milliseconds>,
                   elle::Defaulted<std::chrono::milliseconds>,
                   elle::Defaulted<bool>,
                   boost::optional<std::chrono::milliseconds>,
                   EncryptOptions,
                   bool>(
                     std::forward<Args>(args)...))
      {}
    }
  }
}

ELLE_DAS_SERIALIZE(infinit::model::doughnut::AdminKeys);
ELLE_DAS_SERIALIZE(infinit::model::doughnut::EncryptOptions);
