#include <fstream>

#include <infinit/storage/S3.hh>

#include <elle/factory.hh>
#include <elle/log.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <aws/S3.hh>


#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.model.s3.S3");

namespace infinit
{
  namespace storage
  {

    S3::S3(std::unique_ptr<aws::S3> storage)
    : _storage(std::move(storage))
    {}

    elle::Buffer
    S3::_get(Key key) const
    {
      try
      {
        return _storage->get_object(elle::sprintf("%x", key));
      }
      catch(aws::AWSException const& e)
      {
        ELLE_TRACE("aws exception: %s", e);
        throw MissingKey(key);
      }
    }
    void
    S3::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      _storage->put_object(value, elle::sprintf("%x", key));
    }
    void
    S3::_erase(Key key)
    {
      _storage->delete_object(elle::sprintf("%x", key));
    }
    std::vector<Key>
    S3::_list()
    {
      throw std::runtime_error("Not implemented");
    }
    static std::unique_ptr<Storage> make(std::vector<std::string> const& args)
    {
      std::ifstream is(args[0]);
      elle::serialization::json::SerializerIn input(is);
      aws::Credentials creds(input);
      creds.skew(boost::posix_time::time_duration());
      auto s3 = elle::make_unique<aws::S3>(creds);
      return elle::make_unique<infinit::storage::S3>(std::move(s3));
    }


    struct S3StorageConfig:
    public StorageConfig
    {
    public:
      std::string configuration;
      S3StorageConfig(elle::serialization::SerializerIn& input)
        : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        StorageConfig::serialize(s);
        s.serialize("configuration", this->configuration);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        std::ifstream is(configuration);
        elle::serialization::json::SerializerIn input(is);
        aws::Credentials creds(input);
        creds.skew(boost::posix_time::time_duration());
        auto s3 = elle::make_unique<aws::S3>(creds);
        return elle::make_unique<infinit::storage::S3>(std::move(s3));
      }
    };

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<S3StorageConfig>
    _register_S3StorageConfig("s3");
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "s3", &infinit::storage::make);
