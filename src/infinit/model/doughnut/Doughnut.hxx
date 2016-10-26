#ifndef INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HXX
# define INFINIT_MODEL_DOUGHNUT_DOUGHNUT_HXX

# include <elle/named.hh>

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

      NAMED_ARGUMENT(id);
      NAMED_ARGUMENT(name);
      NAMED_ARGUMENT(keys);
      NAMED_ARGUMENT(owner);
      NAMED_ARGUMENT(passport);
      NAMED_ARGUMENT(consensus_builder);
      NAMED_ARGUMENT(overlay_builder);
      NAMED_ARGUMENT(port);
      NAMED_ARGUMENT(listen_address);
      NAMED_ARGUMENT(storage);
      NAMED_ARGUMENT(version);
      NAMED_ARGUMENT(admin_keys);
      NAMED_ARGUMENT(rdv_host);
      NAMED_ARGUMENT(monitoring_socket_path);
      NAMED_ARGUMENT(protocol);

      struct Doughnut::Init
      {
        Address id;
        std::shared_ptr<cryptography::rsa::KeyPair> keys;
        std::shared_ptr<cryptography::rsa::PublicKey> owner;
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
      };

      template <typename ... Args>
      Doughnut::Doughnut(Args&& ... args)
        : Doughnut(
          elle::named::prototype(
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
            doughnut::protocol = Protocol::all
            ).call(
              [] (Address id,
                  std::shared_ptr<cryptography::rsa::KeyPair> keys,
                  std::shared_ptr<cryptography::rsa::PublicKey> owner,
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
                  Protocol p)
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
                };
              },
              std::forward<Args>(args)...))
      {}
    }
  }
}

#endif
