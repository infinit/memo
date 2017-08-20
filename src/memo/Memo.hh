#pragma once

#include <string>

#include <boost/signals2.hpp>

#include <elle/flat-set.hh>
#include <elle/system/XDG.hh>

#include <memo/KeyValueStore.hh>
#include <memo/LoginCredentials.hh>
#include <memo/Network.hh>
#include <memo/User.hh>
#include <memo/credentials/AWSCredentials.hh>
#include <memo/credentials/Credentials.hh>
#include <memo/credentials/OAuthCredentials.hh>
#include <memo/utility.hh>

namespace memo
{
  namespace bfs = boost::filesystem;

  /// This object is used to manipulate resources used by Memo.
  class Memo
  {
  public:
    Memo();

  public:
    /// ReportAction is a signal Memo triggers when it performs an
    /// action related to the resources / objects (e.g. Users,
    /// Networks, etc.).
    using ReportAction =
      boost::signals2::signal<auto (std::string const& action,
                                    std::string const& type,
                                    std::string const& name) -> void>;

  public:
    /// Whether the resource name contains a `/`.
    static bool
    is_qualified_name(std::string const& object_name);
    /// "<owner_name>/<object_name>" unless object_name is already
    /// qualified.
    static std::string
    qualified_name(std::string const& object_name, User const& owner);
    /// Return "<owner>" from "<owner>/<object>".
    static std::string
    owner_name(std::string const& qualified_name);

    /*----------.
    | Network.  |
    `----------*/

    /// The network for a given user and name.
    ///
    /// @param name The name of the network to get. If the name is unqualifed,
    ///             it gets qualified by the user name.
    /// @param user The potential owner of the network.
    /// @param ensure_linked If true and the network is not linked, raise.
    ///
    /// @return The network.
    ///
    /// @raise MissingLocalResource if the network doesn't exist.
    /// @raise elle::Error if ensure_linked is provided and the network is not
    ///                    linked.
    Network
    network_get(std::string const& name,
                User const& user,
                bool ensure_linked = true);
    /// The networks associated with a user (or by default, by the
    /// default memo user).
    ///
    /// @param self         The user.
    /// @param linked_only  Filter non-linked networks.
    ///
    /// @return The list of networks.
    std::vector<Network>
    networks_get(boost::optional<memo::User> self,
                 bool linked_only = false) const;
    /// The list of users who linked the given network.
    ///
    /// @param name The name of the network.
    /// @param user An optional user used to qualify unqualifed network names.
    ///
    /// @return The list of user.
    std::vector<User>
    network_linked_users(std::string const& name,
                         boost::optional<User> user = boost::none);

    void
    network_unlink(std::string const& name_,
                   User const& user);
    bool
    network_delete(std::string const& name_,
                   User const& user,
                   bool unlink);
    NetworkDescriptor
    network_descriptor_get(std::string const& name_,
                           User const& owner,
                           bool or_network = true);
    void
    network_save(NetworkDescriptor const& network, bool overwrite = false) const;
    void
    network_save(memo::User const& self,
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
    bool
    passport_delete(Passport const& passport);
    bool
    passport_delete(std::string const& network_name,
                    std::string const& user_name);
    /*-------.
    | User.  |
    `-------*/

    void
    user_save(User const& user,
              bool overwrite = false);
    bool
    user_delete(User const& user);
    User
    user_get(std::string const& user, bool hub_fallback = false) const;
    std::vector<User>
    users_get() const;
    bfs::path
    _avatars_path() const;
    bfs::path
    _avatar_path(std::string const& name) const;
    bool
    avatar_delete(User const& user);

    /*-------.
    | Silo.  |
    `-------*/

    using SiloConfigPtr = std::unique_ptr<silo::SiloConfig>;
    SiloConfigPtr
    silo_get(std::string const& name);

    struct SiloByName
    {
      bool
      operator() (SiloConfigPtr const& lhs,
                  SiloConfigPtr const& rhs) const
      {
        return lhs->name < rhs->name;
      }
    };

    using Silos = boost::container::flat_set<SiloConfigPtr, SiloByName>;

    Silos
    silos_get();
    void
    silo_save(std::string const& name,
              SiloConfigPtr const& silo);
    bool
    silo_delete(SiloConfigPtr const& silo,
                bool clear = false);
    std::unordered_map<std::string, std::vector<std::string>>
    silo_networks(std::string const& silo_name);

    /*------------------.
    | Key Value Store.  |
    `------------------*/

    bool
    key_value_store_has(std::string const& name);
    KeyValueStore
    key_value_store_get(std::string const& name);
    void
    key_value_store_save(KeyValueStore const& kvs, bool overwrite = false);
    bool
    key_value_store_delete(KeyValueStore const& kvs);
    std::vector<KeyValueStore>
    key_value_stores_get() const;

    /*--------------.
    | Credentials.  |
    `--------------*/

    void
    credentials_add(std::string const& name, std::unique_ptr<Credentials> a);
    bool
    credentials_delete(std::string const& type, std::string const& name);
    template <typename T = memo::Credentials>
    std::vector<std::unique_ptr<T, std::default_delete<memo::Credentials>>>
    credentials(std::string const& name) const;
    std::unique_ptr<Credentials>
    credentials(std::string const& name, std::string const& identifier) const;
    void
    credentials_aws_add(std::unique_ptr<Credentials> a);
    std::vector<std::unique_ptr<AWSCredentials,
                                std::default_delete<memo::Credentials>>>
    credentials_aws() const;
    std::unique_ptr<AWSCredentials, std::default_delete<memo::Credentials>>
    credentials_aws(std::string const& uid) const;
    void
    credentials_dropbox_add(std::unique_ptr<Credentials> a);
    std::vector<
      std::unique_ptr<
      OAuthCredentials,
        std::default_delete<memo::Credentials>
        >
      >
    credentials_dropbox() const;
    std::unique_ptr<OAuthCredentials, std::default_delete<memo::Credentials>>
      credentials_dropbox(std::string const& uid) const;
    void
    credentials_google_add(std::unique_ptr<Credentials> a);
    std::vector<
      std::unique_ptr<
        OAuthCredentials,
        std::default_delete<memo::Credentials>
        >
      >
    credentials_google() const;
    std::unique_ptr<OAuthCredentials, std::default_delete<memo::Credentials>>
      credentials_google(std::string const& uid) const;
    void
    credentials_gcs_add(std::unique_ptr<Credentials> a);
    std::vector<
      std::unique_ptr<
        OAuthCredentials,
        std::default_delete<memo::Credentials>
        >
      >
    credentials_gcs() const;
    std::unique_ptr<OAuthCredentials, std::default_delete<memo::Credentials>>
      credentials_gcs(std::string const& uid) const;
    bfs::path
    _credentials_path() const;
    bfs::path
    _credentials_path(std::string const& service) const;
    bfs::path
    _credentials_path(std::string const& service, std::string const& name) const;
    bfs::path
    _network_descriptors_path() const;
    bfs::path
    _network_descriptor_path(std::string const& name) const;
    bfs::path
    _networks_path(bool create_dir = true) const;
    bfs::path
    _networks_path(User const& user, bool create_dir = true) const;
    bfs::path
    _network_path(std::string const& name,
                  User const& user,
                  bool create_dir = true) const;
    bfs::path
    _network_cache_dir(std::string const& name,
                       User const& user) const;
    bfs::path
    _network_monitoring_socket_path(std::string const& name,
                                    User const& user) const;
    bfs::path
    _passports_path() const;
    bfs::path
    _passport_path(std::string const& network, std::string const& user) const;
    bfs::path
    _silos_path() const;
    bfs::path
    _silo_path(std::string const& name) const;
    bfs::path
    _users_path() const;
    bfs::path
    _user_path(std::string const& name) const;
    bfs::path
    _key_value_stores_path() const;
    bfs::path
    _key_value_store_path(std::string const& name) const;
    static
    void
    _open_read(bfs::ifstream& f,
               bfs::path const& path,
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
    /// @param f The stream to associate with the file.
    /// @param path The path to the file to open.
    /// @param name The name of the resource (e.g. "root")
    /// @param type The type of the resource (e.g. "User")
    /// @param overwrite Whether if the function is allowed to overwrite an
    ///                  existing file.
    /// @param mode Flags describing the requested input/output mode for the
    ///                   file.
    /// @return Whether if a file was overwritten.
    /// @throw ResourceAlreadyFetched if the file already exists and overwrite
    ///        was false.
    static
    bool
    _open_write(bfs::ofstream& f,
                bfs::path const& path,
                std::string const& name,
                std::string const& type,
                bool overwrite = false,
                std::ios_base::openmode mode = std::ios_base::out);

    std::vector<std::string>
    user_passports_for_network(std::string const& network_name);
    std::vector<KeyValueStore>
    key_value_stores_for_network(std::string const& network_name);
    // saving & loading
    template <typename T>
    static
    T
    load(std::ifstream& input);
    template <typename T>
    static
    void
    save(std::ostream& output, T const& resource, bool pretty = true);
  protected:
    bool
    _delete(bfs::path const& path,
            std::string const& type,
            std::string const& name);
    bool
    _delete_all(bfs::path const& path,
                std::string const& type,
                std::string const& name);
  public:
    // Beyond
    enum class PushResult
    {
      pushed,
      updated,
      alreadyPushed,
    };

    elle::json::Json
    hub_login(std::string const& name, LoginCredentials const& o) const;

    /// Push a payload to the hub.
    ///
    /// @param where The route to put the resource to (<hub_url>/<where>).
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param payload The payload to push.
    /// @param content_type The content type of the payload (e.g.
    ///                     application/json)
    /// @param self The user to perform the request as.
    /// @param extra_headers Headers to add to the HTTP request.
    ///
    /// @return A PushResult, containing information related to the attempt.
    PushResult
    hub_push_data(std::string const& where,
                  std::string const& type,
                  std::string const& name,
                  elle::ConstWeakBuffer const& object,
                  std::string const& content_type,
                  User const& self,
                  bool hub_error = false,
                  bool update = false) const;

    /// Push an arbitrary resource to the hub.
    ///
    /// @param where The route to put the resource to (<hub_url>/<where>).
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param o The resource.
    /// @param self The user to that own the resource (who must be registered
    ///             if the resource is not a user).
    /// @param hub_error Turn exception to HubError.
    /// @paran update Whether the operation is an update (and not an insertion).
    ///
    /// @raise HubError for all errors if hub_error is provided, otherwise if
    ///        the error is not part of the following Errors.
    /// @raise MissingResource if the resource doesn't exist on the hub.
    /// @raise ResourceGone if the resource is gone from the hub.
    template <typename Serializer = void, typename T>
    void
    hub_push(std::string const& where,
             std::string const& type,
             std::string const& name,
             T const& o,
             memo::User const& self,
             bool hub_error = false,
             bool update = false) const;

    /// Push an arbitrary resource to the hub.
    ///
    /// Compare to the other hub_push method, the `where` is computed using
    /// `<type>s/<name>`, example:
    /// for type = network and name = root/my_net, `where` will be equal to
    /// `networks/root/my_net`.
    ///
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param o The resource.
    /// @param self The user to that own the resource (who must be registered
    ///             if the resource is not a user).
    /// @param hub_error Turn exception to HubError.
    /// @paran update Whether the operation is an update (and not an insertion).
    ///
    /// @raise HubError for all errors if hub_error is provided, otherwise if
    ///        the error is not part of the following Errors.
    /// @raise MissingResource if the resource doesn't exist on the hub.
    /// @raise ResourceGone if the resource is gone from the hub.
    template <typename Serializer = void, typename T>
    void
    hub_push(std::string const& type,
             std::string const& name,
             T const& o,
             memo::User const& self,
             bool hub_error = false,
             bool update = false) const;

    /// Perform a fetch request.
    ///
    /// @param where The route to put the resource to (<hub_url>/<where>).
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param self The user to perform the request as.
    /// @param extra_headers Headers to add to the HTTP request.
    ///
    /// @return The request.
    ///
    /// @raise HubError for all errors if hub_error is provided, otherwise if
    ///        the error is not part of the following Errors.
    /// @raise MissingResource if the resource doesn't exist on the hub.
    /// @raise ResourceGone if the resource is gone from the hub.
    /// @raise ResourceProtected if you access is denied.
    std::unique_ptr<elle::reactor::http::Request>
    hub_fetch_request(std::string const& where,
                      std::string const& type,
                      std::string const& name,
                      boost::optional<User const&> self = boost::none,
                      Headers const& extra_headers = {}) const;

    /// Perform a fetch request and return the extracted json.
    ///
    /// @param where The route to put the resource to (<hub_url>/<where>).
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param self The user to perform the request as.
    /// @param extra_headers Headers to add to the HTTP request.
    ///
    /// @return The json.
    ///
    /// @raise HubError for all errors if hub_error is provided, otherwise if
    ///        the error is not part of the following Errors.
    /// @raise MissingResource if the resource doesn't exist on the hub.
    /// @raise ResourceGone if the resource is gone from the hub.
    /// @raise ResourceProtected if you access is denied.
    elle::json::Json
    hub_fetch_json(std::string const& where,
                   std::string const& type,
                   std::string const& name,
                   boost::optional<User const&> self = boost::none,
                   Headers const& extra_headers = {}) const;

    /// Fetch a resource from the hub.
    ///
    /// @param where The route to put the resource to (<hub_url>/<where>).
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param self The user to perform the request as.
    /// @param extra_headers Headers to add to the HTTP request.
    ///
    /// @return The fecthed resource.
    ///
    /// @raise HubError for all errors if hub_error is provided, otherwise if
    ///        the error is not part of the following Errors.
    /// @raise MissingResource if the resource doesn't exist on the hub.
    /// @raise ResourceGone if the resource is gone from the hub.
    /// @raise ResourceProtected if you access is denied.
    template <typename T>
    T
    hub_fetch(std::string const& where,
              std::string const& type,
              std::string const& name,
              boost::optional<memo::User const&> self = boost::none,
              memo::Headers const& extra_headers = memo::Headers{}) const;

    /// Fetch a resource from the hub.
    ///
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    ///
    /// @return The fecthed resource.
    ///
    /// @raise HubError for all errors if hub_error is provided, otherwise if
    ///        the error is not part of the following Errors.
    /// @raise MissingResource if the resource doesn't exist on the hub.
    /// @raise ResourceGone if the resource is gone from the hub.
    /// @raise ResourceProtected if you access is denied.
    template <typename T>
    T
    hub_fetch(std::string const& type,
              std::string const& name) const;

    /// Delete a resource from the hub.
    ///
    /// @param where The route to put the resource to (<hub_url>/<where>).
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param self The user to perform the request as.
    /// @param ignore_missing Do not consider removing a resource already gone
    ///                       an error.
    /// @param purge Remove all resources depending on the one your deleting.
    ///
    /// @return Whether the resource was deleted.
    bool
    hub_delete(std::string const& where,
               std::string const& type,
               std::string const& name,
               User const& self,
               bool ignore_missing = false,
               bool purge = false) const;

    /// Delete a resource from the hub.
    ///
    /// @param type The type of resource (e.g. network, volume. etc.).
    /// @param name The name of the resource.
    /// @param self The user to perform the request as.
    /// @param ignore_missing Do not consider removing a resource already gone
    ///                       an error.
    /// @param purge Remove all resources depending on the one your deleting.
    ///
    /// @return Whether the resource was deleted.
    bool
    hub_delete(std::string const& type,
               std::string const& name,
               User const& self,
               bool ignore_missing = false,
               bool purge = false) const;

  private:
    /// report_local_action is triggered when a local resource is edited:
    /// - saved
    /// - updated
    /// - deleted
    ELLE_ATTRIBUTE_RX(ReportAction, report_local_action);
    /// report_remote_action is triggered when a resource is edited on the hub:
    /// - saved
    /// - updated
    /// - deleted
    ELLE_ATTRIBUTE_RX(ReportAction, report_remote_action);
  };

  namespace deprecated
  {
    bfs::path
    storages_path();
  }
}

#include <memo/Memo.hxx>
