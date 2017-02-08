#pragma once

#include <string>

#include <infinit/Drive.hh>
#include <infinit/LoginCredentials.hh>
#include <infinit/Network.hh>
#include <infinit/User.hh>
#include <infinit/credentials/AWSCredentials.hh>
#include <infinit/credentials/Credentials.hh>
#include <infinit/credentials/OAuthCredentials.hh>
#include <infinit/utility.hh>

namespace infinit
{
  class Infinit
  {
  public:
    using Reporter = std::function<void (std::string const&)>;

  public:
    /// Whether has a `/`.
    static bool
    is_qualified_name(std::string const& object_name);
    /// "<owner.name>/<object_name>" unless object_name is already
    /// qualified.
    static std::string
    qualified_name(std::string const& object_name, User const& owner);
    /// Return "<owner>" from "<owner>/<object>".
    static std::string
    owner_name(std::string const& qualified_name);

    /*----------.
    | Network.  |
    `----------*/

    /// Takes care of the `qualified_name` part.
    Network
    network_get(std::string const& name_,
                User const& user,
                bool require_model = true);
    std::vector<Network>
    networks_get(boost::optional<infinit::User> self,
                 bool require_linked = false) const;
    std::vector<User>
    network_linked_users(std::string const& name_,
                         boost::optional<User> user = boost::none);
    void
    network_unlink(std::string const& name_,
                   User const& user,
                   Reporter report = {});
    void
    network_delete(std::string const& name_,
                   User const& user,
                   bool unlink,
                   Reporter report = {});
    std::vector<Drive>
    drives_get() const;
    NetworkDescriptor
    network_descriptor_get(std::string const& name_,
                           User const& owner,
                           bool or_network = true);
    void
    network_save(NetworkDescriptor const& network, bool overwrite = false) const;
    void
    network_save(infinit::User const& self,
                 Network const& network, bool overwrite = false) const;

    /*-----------.
    | Passport.  |
    `-----------*/

    using Passport = model::doughnut::Passport;
    Passport
    passport_get(std::string const& network, std::string const& user);

    std::vector<std::pair<Passport, std::string>>
    passports_get(boost::optional<std::string> network = boost::none);

    void
    passport_save(Passport const& passport, bool overwrite = false);

    /*-------.
    | User.  |
    `-------*/

    void
    user_save(User const& user,
              bool overwrite = false);
    User
    user_get(std::string const& user, bool beyond_fallback = false) const;
    std::vector<User>
    users_get() const;
    boost::filesystem::path
    _user_avatar_path() const;
    boost::filesystem::path
    _user_avatar_path(std::string const& name) const;


    /*----------.
    | Storage.  |
    `----------*/

    std::unique_ptr<storage::StorageConfig>
    storage_get(std::string const& name);
    std::vector<std::unique_ptr<storage::StorageConfig>>
    storages_get();
    void
    storage_save(std::string const& name,
                 std::unique_ptr<storage::StorageConfig> const& storage);
    void
    storage_remove(std::string const& name);
    std::unordered_map<std::string, std::vector<std::string>>
    storage_networks(std::string const& storage_name);


    /*---------.
    | Volume.  |
    `---------*/

    bool
    volume_has(std::string const& name);
    Volume
    volume_get(std::string const& name);
    void
    volume_save(Volume const& volume, bool overwrite = false);
    std::vector<Volume>
    volumes_get() const;


    /*--------------.
    | Credentials.  |
    `--------------*/

    void
    credentials_add(std::string const& name, std::unique_ptr<Credentials> a);
    template <typename T = infinit::Credentials>
    std::vector<std::unique_ptr<T, std::default_delete<infinit::Credentials>>>
    credentials(std::string const& name) const;
    std::unique_ptr<Credentials>
    credentials(std::string const& name, std::string const& identifier) const;
    void
    credentials_aws_add(std::unique_ptr<Credentials> a);
    std::vector<std::unique_ptr<AWSCredentials,
                                std::default_delete<infinit::Credentials>>>
    credentials_aws() const;
    std::unique_ptr<AWSCredentials, std::default_delete<infinit::Credentials>>
    credentials_aws(std::string const& uid) const;
    void
    credentials_dropbox_add(std::unique_ptr<Credentials> a);
    std::vector<
      std::unique_ptr<
      OAuthCredentials,
        std::default_delete<infinit::Credentials>
        >
      >
    credentials_dropbox() const;
    std::unique_ptr<OAuthCredentials, std::default_delete<infinit::Credentials>>
      credentials_dropbox(std::string const& uid) const;
    void
    credentials_google_add(std::unique_ptr<Credentials> a);
    std::vector<
      std::unique_ptr<
        OAuthCredentials,
        std::default_delete<infinit::Credentials>
        >
      >
    credentials_google() const;
    std::unique_ptr<OAuthCredentials, std::default_delete<infinit::Credentials>>
      credentials_google(std::string const& uid) const;
    void
    credentials_gcs_add(std::unique_ptr<Credentials> a);
    std::vector<
      std::unique_ptr<
        OAuthCredentials,
        std::default_delete<infinit::Credentials>
        >
      >
    credentials_gcs() const;
    std::unique_ptr<OAuthCredentials, std::default_delete<infinit::Credentials>>
      credentials_gcs(std::string const& uid) const;
    boost::filesystem::path
    _credentials_path() const;
    boost::filesystem::path
    _credentials_path(std::string const& service) const;
    boost::filesystem::path
    _credentials_path(std::string const& service, std::string const& name) const;
    boost::filesystem::path
    _network_descriptors_path() const;
    boost::filesystem::path
    _network_descriptor_path(std::string const& name) const;
    boost::filesystem::path
    _networks_path(bool create_dir = true) const;
    boost::filesystem::path
    _networks_path(User const& user, bool create_dir = true) const;
    boost::filesystem::path
    _network_path(std::string const& name,
                  User const& user,
                  bool create_dir = true) const;
    boost::filesystem::path
    _passports_path() const;
    boost::filesystem::path
    _passport_path(std::string const& network, std::string const& user) const;
    boost::filesystem::path
    _storages_path() const;
    boost::filesystem::path
    _storage_path(std::string const& name) const;
    boost::filesystem::path
    _users_path() const;
    boost::filesystem::path
    _user_path(std::string const& name) const;
    boost::filesystem::path
    _volumes_path() const;
    boost::filesystem::path
    _volume_path(std::string const& name) const;
    static
    void
    _open_read(boost::filesystem::ifstream& f,
               boost::filesystem::path const& path,
               std::string const& name,
               std::string const& type);
    /// Open the given path, associating it with the given ofstream f.
    ///
    /// If overwrite isn't specify and the path already exists, throw
    /// an ResourceAlreadyFetched exception.
    ///
    /// Otherwise, return whether the resource already exists and was
    /// overwritten.
    ///
    /// \param f The stream to associate with the file.
    /// \param path The path to the file to open.
    /// \param name The name of the resource (e.g. "root")
    /// \param type The type of the resource (e.g. "User")
    /// \param overwrite Whether if the function is allowed to overwrite an
    ///                  existing file.
    /// \param mode Flags describing the requested input/output mode for the
    ///                   file.
    /// \return Whether if a file was overwritten.
    /// \throw ResourceAlreadyFetched if the file already exists and overwrite
    ///        was false.
    static
    bool
    _open_write(boost::filesystem::ofstream& f,
                boost::filesystem::path const& path,
                std::string const& name,
                std::string const& type,
                bool overwrite = false,
                std::ios_base::openmode mode = std::ios_base::out);
    boost::filesystem::path
    _drives_path() const;
    boost::filesystem::path
    _drive_path(std::string const& name) const;
    void
    drive_save(Drive const& drive,
               bool overwrite = true);
    Drive
    drive_get(std::string const& name);
    bool
    drive_delete(std::string const& name);
    boost::filesystem::path
    _drive_icon_path() const;
    boost::filesystem::path
    _drive_icon_path(std::string const& name) const;
    Drive
    drive_fetch(std::string const& name);

    std::vector<std::string>
    user_passports_for_network(std::string const& network_name);
    std::vector<std::string>
    volumes_for_network(std::string const& network_name);
    std::vector<std::string>
    drives_for_volume(std::string const& volume_name);
    // saving & loading
    template <typename T>
    static
    T
    load(std::ifstream& input);
    template <typename T>
    static
    void
    save(std::ostream& output, T const& resource, bool pretty = true);

    // Beyond
    enum class PushResult
    {
      pushed,
      updated,
      alreadyPushed,
    };

    elle::json::Json
    beyond_login(std::string const& name, LoginCredentials const& o) const;

    std::unique_ptr<reactor::http::Request>
    beyond_fetch_data(std::string const& where,
                      std::string const& type,
                      std::string const& name,
                      boost::optional<User const&> self = boost::none,
                      Headers const& extra_headers = {}) const;
    elle::json::Json
    beyond_fetch_json(std::string const& where,
                      std::string const& type,
                      std::string const& name,
                      boost::optional<User const&> self = boost::none,
                      Headers const& extra_headers = {}) const;
    bool
    beyond_delete(std::string const& where,
                  std::string const& type,
                  std::string const& name,
                  User const& self,
                  bool ignore_missing = false,
                  bool purge = false) const;
    bool
    beyond_delete(std::string const& type,
                  std::string const& name,
                  User const& self,
                  bool ignore_missing = false,
                  bool purge = false) const;
    PushResult
    beyond_push_data(std::string const& where,
                     std::string const& type,
                     std::string const& name,
                     elle::ConstWeakBuffer const& object,
                     std::string const& content_type,
                     User const& self,
                     bool beyond_error = false,
                     bool update = false) const;
    template <typename T>
    T
    beyond_fetch(std::string const& where,
                 std::string const& type,
                 std::string const& name,
                 boost::optional<infinit::User const&> self = boost::none,
                 infinit::Headers const& extra_headers = infinit::Headers{}) const;

    template <typename T>
    T
    beyond_fetch(std::string const& type,
                 std::string const& name) const;

    template <typename Serializer = void, typename T>
    void
    beyond_push(std::string const& where,
                std::string const& type,
                std::string const& name,
                T const& o,
                infinit::User const& self,
                bool beyond_error = false,
                bool update = false) const;

    template <typename Serializer = void, typename T>
    void
    beyond_push(std::string const& type,
                std::string const& name,
                T const& o,
                infinit::User const& self,
                bool beyond_error = false,
                bool update = false) const;
  };
}

#include <infinit/Infinit.hxx>
