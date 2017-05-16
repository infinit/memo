#pragma once

#include <elle/service/aws/S3.hh>

#include <infinit/silo/Storage.hh>

namespace infinit
{
  namespace silo
  {
    class S3
      : public Storage
    {
    public:
      S3(std::unique_ptr<elle::service::aws::S3> storage,
         elle::service::aws::S3::StorageClass storage_class,
         boost::optional<int64_t> capacity);
      ~S3();
      std::string
      type() const override { return "s3"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;

      ELLE_ATTRIBUTE_RX(std::unique_ptr<elle::service::aws::S3>, storage);
      ELLE_ATTRIBUTE_R(elle::service::aws::S3::StorageClass, storage_class);
    };

    struct S3StorageConfig
      : public StorageConfig
    {
    public:
      using Super = StorageConfig;
      using StorageClass = elle::service::aws::S3::StorageClass;

    public:
      S3StorageConfig(std::string name,
                      elle::service::aws::Credentials credentials,
                      StorageClass storage_class,
                      boost::optional<int64_t> capacity,
                      boost::optional<std::string> description);
      S3StorageConfig(elle::serialization::SerializerIn& input);

      void
      serialize(elle::serialization::Serializer& s) override;
      std::unique_ptr<infinit::silo::Storage>
      make() override;

      elle::service::aws::Credentials credentials;
      elle::service::aws::S3::StorageClass storage_class;
    };
  }
}
