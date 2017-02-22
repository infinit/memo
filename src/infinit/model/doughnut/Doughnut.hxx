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

      ELLE_DAS_SYMBOL(admin_keys);
      ELLE_DAS_SYMBOL(connect_timeout);
      ELLE_DAS_SYMBOL(consensus_builder);
      ELLE_DAS_SYMBOL(id);
      ELLE_DAS_SYMBOL(keys);
      ELLE_DAS_SYMBOL(listen_address);
      ELLE_DAS_SYMBOL(monitoring_socket_path);
      ELLE_DAS_SYMBOL(name);
      ELLE_DAS_SYMBOL(overlay_builder);
      ELLE_DAS_SYMBOL(owner);
      ELLE_DAS_SYMBOL(passport);
      ELLE_DAS_SYMBOL(port);
      ELLE_DAS_SYMBOL(protocol);
      ELLE_DAS_SYMBOL(rdv_host);
      ELLE_DAS_SYMBOL(soft_fail_running);
      ELLE_DAS_SYMBOL(soft_fail_timeout);
      ELLE_DAS_SYMBOL(storage);
      ELLE_DAS_SYMBOL(version);

      struct Doughnut::Init
      {
        Address id;
        std::shared_ptr<elle::cryptography::rsa::KeyPair> keys;
        std::shared_ptr<elle::cryptography::rsa::PublicKey> owner;
        Passport passport;
        ConsensusBuilder consensus;
        OverlayBuilder overlay_builder;
        boost::optional<int> port;
        boost::optional<boost::asio::ip::address> listen_address;
        std::unique_ptr<storage::Storage> storage;
        boost::optional<std::string> name;
        boost::optional<elle::Version> version;
        AdminKeys const& admin_keys;
        boost::optional<std::string> rdv_host;
        boost::optional<boost::filesystem::path> monitoring_socket_path;
        Protocol protocol;
        elle::Defaulted<std::chrono::milliseconds> connect_timeout;
        elle::Defaulted<std::chrono::milliseconds> soft_fail_timeout;
        elle::Defaulted<bool> soft_fail_running;
      };

      template <typename ... Args>
      Doughnut::Doughnut(Args&& ... args)
        : Doughnut(
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
            doughnut::soft_fail_running = elle::defaulted(false)
            ).call(
              [] (Address id,
                  std::shared_ptr<elle::cryptography::rsa::KeyPair> keys,
                  std::shared_ptr<elle::cryptography::rsa::PublicKey> owner,
                  Passport passport,
                  ConsensusBuilder consensus,
                  OverlayBuilder overlay_builder,
                  boost::optional<int> port,
                  boost::optional<boost::asio::ip::address> listen_address,
                  std::unique_ptr<storage::Storage> storage,
                  boost::optional<std::string> name,
                  boost::optional<elle::Version> version,
                  AdminKeys const& admin_keys,
                  boost::optional<std::string> rdv_host,
                  boost::optional<boost::filesystem::path> monitoring_socket_path,
                  Protocol p,
                  elle::Defaulted<std::chrono::milliseconds> connect_timeout,
                  elle::Defaulted<std::chrono::milliseconds> soft_fail_timeout,
                  elle::Defaulted<bool> soft_fail_running)
              -> Init
              {
                return Init{
                  std::move(id),
                  std::move(keys),
                  std::move(owner),
                  std::move(passport),
                  std::move(consensus),
                  std::move(overlay_builder),
                  std::move(port),
                  std::move(listen_address),
                  std::move(storage),
                  std::move(name),
                  std::move(version),
                  std::move(admin_keys),
                  std::move(rdv_host),
                  std::move(monitoring_socket_path),
                  std::move(p),
                  std::move(connect_timeout),
                  std::move(soft_fail_timeout),
                  std::move(soft_fail_running),
                };
              },
              std::forward<Args>(args)...))
      {}
    }
  }
}

ELLE_DAS_SERIALIZE(infinit::model::doughnut::AdminKeys);
