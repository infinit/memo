#ifndef INFINIT_STORAGE_S3_HH
# define INFINIT_STORAGE_S3_HH

# include <infinit/storage/Storage.hh>
# include <elle/service/aws/S3.hh>

namespace infinit
{
  namespace storage
  {
    class S3
      : public Storage
    {
    public:
      S3(std::unique_ptr<elle::service::aws::S3> storage,
         elle::service::aws::S3::StorageClass storage_class,
         boost::optional<int64_t> capacity);
      ~S3();

    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      int
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;

      ELLE_ATTRIBUTE_RX(std::unique_ptr<elle::service::aws::S3>, storage);
      ELLE_ATTRIBUTE_R(elle::service::aws::S3::StorageClass, storage_class);
    };

    struct S3StorageConfig
      : public StorageConfig
    {
    public:
      typedef elle::service::aws::S3::StorageClass StorageClass;

    public:
      S3StorageConfig(std::string name,
                      elle::service::aws::Credentials credentials,
                      elle::service::aws::S3::StorageClass storage_class,
                      boost::optional<int64_t> capacity,
                      boost::optional<std::string> description);
      S3StorageConfig(elle::serialization::SerializerIn& input);

      virtual
      void
      serialize(elle::serialization::Serializer& s) override;
      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override;

      elle::service::aws::Credentials credentials;
      elle::service::aws::S3::StorageClass storage_class;
    };
  }
}

#endif
