#ifndef INFINIT_STORAGE_GCS_HH
# define INFINIT_STORAGE_GCS_HH

# include <boost/filesystem.hpp>

# include <elle/Error.hh>

# include <infinit/storage/Storage.hh>
# include <infinit/storage/Key.hh>
# include <infinit/storage/GoogleAPI.hh>

# include <reactor/http/Request.hh>
# include <reactor/http/url.hh>

namespace infinit
{
  namespace storage
  {
    class GCS
      : public Storage, public GoogleAPI
    {
    public:
      GCS(std::string const& name,
          std::string const& bucket,
          std::string const& root,
          std::string const& refresh_token);
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

      ELLE_ATTRIBUTE_R(std::string, bucket);
      ELLE_ATTRIBUTE_R(std::string, root);

      std::string
      _url(Key key) const;
    };

    struct GCSConfig: public StorageConfig
    {
      GCSConfig(std::string const& name,
                std::string const& bucket,
                std::string const& root,
                std::string const& user_name,
                std::string const& refresh_token,
                boost::optional<int64_t> capacity);
      GCSConfig(elle::serialization::SerializerIn& input);
      void serialize(elle::serialization::Serializer& s) override;
      virtual std::unique_ptr<infinit::storage::Storage> make() override;
      std::string bucket;
      std::string root;
      std::string refresh_token;
      std::string user_name;
    };
  }
}

#endif

