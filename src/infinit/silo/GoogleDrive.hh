#pragma once

#include <boost/filesystem.hpp>

#include <elle/Error.hh>

#include <infinit/silo/Storage.hh>
#include <infinit/silo/Key.hh>
#include <infinit/silo/GoogleAPI.hh>

#include <elle/reactor/Barrier.hh>
#include <elle/reactor/http/Request.hh>
#include <elle/reactor/http/url.hh>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Scope.hh>

namespace infinit
{
  namespace storage
  {
    class GoogleDrive
      : public Storage, public GoogleAPI
    {
    public:
      GoogleDrive(std::string refresh_token,
                  std::string name);
      GoogleDrive(boost::filesystem::path root,
                  std::string refresh_token,
                  std::string name);
      ~GoogleDrive() = default;
      std::string
      type() const override { return "googledrive"; }

    protected:
      elle::Buffer
      _get(Key k) const override;

      int
      _set(Key k,
           elle::Buffer const& value,
           bool insert,
           bool update) override;

      int
      _erase(Key k) override;

      std::vector<Key>
      _list() override;

      BlockStatus
      _status(Key k) override;

    private:
      ELLE_ATTRIBUTE_R(boost::filesystem::path, root);
      ELLE_ATTRIBUTE_R(std::string, dir_id);

      boost::filesystem::path _path(Key key) const;

      // Create a directory on GoogleDrive.
      elle::reactor::http::Request
      _mkdir(std::string const& path) const;

      // Insert a file on GoogleDrive.
      elle::reactor::http::Request
      _insert(Key key, elle::Buffer const& value) const;

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
                               boost::optional<int64_t> capacity,
                               boost::optional<std::string> description);
      GoogleDriveStorageConfig(elle::serialization::SerializerIn& input);
      void serialize(elle::serialization::Serializer& s) override;
      virtual std::unique_ptr<infinit::storage::Storage> make() override;

      boost::optional<std::string> root;
      std::string refresh_token;
      std::string user_name;
    };
  }
}
