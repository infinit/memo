#ifndef INFINIT_STORAGE_GOOGLEDRIVE_HH
# define INFINIT_STORAGE_GOOGLEDRIVE_HH

# include <boost/filesystem.hpp>

# include <elle/Error.hh>

# include <infinit/storage/Storage.hh>
# include <infinit/storage/Key.hh>

# include <reactor/Barrier.hh>
# include <reactor/http/Request.hh>
# include <reactor/http/url.hh>
# include <reactor/scheduler.hh>
# include <reactor/Scope.hh>

namespace infinit
{
  namespace storage
  {
    class GoogleDrive
      : public Storage
    {
      public:

        GoogleDrive(std::string refresh_token,
                    std::string name);
        GoogleDrive(boost::filesystem::path root,
                    std::string refresh_token,
                    std::string name);
        ~GoogleDrive() = default;

      protected:

        virtual
        elle::Buffer
        _get(Key k) const override;

        virtual
        int
        _set(Key k,
             elle::Buffer const& value,
             bool insert,
             bool update) override;

        virtual
        int
        _erase(Key k) override;

        virtual
        std::vector<Key>
        _list() override;

        virtual
        BlockStatus
        _status(Key k) override;

      private:
        ELLE_ATTRIBUTE_R(std::string, token);
        ELLE_ATTRIBUTE_R(boost::filesystem::path, root);
        ELLE_ATTRIBUTE_R(std::string, refresh_token);
        ELLE_ATTRIBUTE_R(std::string, dir_id);
        ELLE_ATTRIBUTE_R(std::string, name);

        boost::filesystem::path _path(Key key) const;

        reactor::http::Request
        _request(std::string url,
                 reactor::http::Method method,
                 reactor::http::Request::QueryDict query,
                 reactor::http::Request::Configuration conf
                   = reactor::http::Request::Configuration{},
                 std::vector<reactor::http::StatusCode>
                 = std::vector<reactor::http::StatusCode>{}) const;

        // Create a directory on GoogleDrive.
        reactor::http::Request
        _mkdir(std::string const& path) const;

        // Insert a file on GoogleDrive.
        reactor::http::Request
        _insert(Key key, elle::Buffer const& value) const;

        // Ask for a new access_token
        void
        _refresh();

        /* Check if a file exists.
         *
         * Returns the file id if found, else empty string.
         */
        std::string
        _exists(std::string file) const;
    };

    struct GoogleDriveStorageConfig
      : public StorageConfig
    {
      GoogleDriveStorageConfig(std::string name,
                               boost::optional<std::string> root,
                               std::string refresh_token,
                               std::string user_name,
                               int64_t capacity = 0);
      GoogleDriveStorageConfig(elle::serialization::SerializerIn& input);
      void serialize(elle::serialization::Serializer& s) override;
      virtual std::unique_ptr<infinit::storage::Storage> make() override;

      boost::optional<std::string> root;
      std::string refresh_token;
      std::string user_name;
    };
  }
}

#endif /* !INFINIT_STORAGE_GOOGLEDRIVE_HH */
