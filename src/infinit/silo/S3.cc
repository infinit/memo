#include <fstream>

#include <infinit/silo/S3.hh>

#include <elle/log.hh>
#include <elle/bench.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <elle/serialization/json/Error.hh> // serialization::MissingKey.
#include <elle/service/aws/S3.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <infinit/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.S3");

#define BENCH(name)                                      \
  static elle::Bench bench("bench.s3store." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

namespace infinit
{
  namespace silo
  {
    S3::S3(std::unique_ptr<elle::service::aws::S3> storage,
           elle::service::aws::S3::StorageClass storage_class,
           boost::optional<int64_t> capacity)
      : Storage(std::move(capacity))
      , _storage(std::move(storage))
      , _storage_class(storage_class)
    {}

    S3::~S3()
    {}

    elle::Buffer
    S3::_get(Key key) const
    {
      BENCH("get");
      try
      {
        return this->_storage->get_object(elle::sprintf("%x", key));
      }
      catch (elle::service::aws::AWSException const& e)
      {
        if (e.inner_exception())
        {
          try
          {
            std::rethrow_exception(e.inner_exception());
          }
          catch (elle::service::aws::FileNotFound const& e)
          {
            ELLE_TRACE("unable to GET block: %s", e);
            throw MissingKey(key);
          }
        }
        else
        {
          throw e;
        }
      }
    }

    int
    S3::_set(Key key, elle::Buffer const& value, bool insert, bool update)
    {
      // FIXME: properly handle insert and update flags.
      BENCH("set");
      ELLE_DEBUG("set %x", key);
      // FIXME: Use multipart upload for blocks bigger than 5 MiB.
      this->_storage->put_object(value,
                                 elle::sprintf("%x", key),
                                 elle::service::aws::RequestQuery(),
                                 this->storage_class());
      return 0;
    }

    int
    S3::_erase(Key key)
    {
      try
      {
        this->_storage->delete_object(elle::sprintf("%x", key));
      }
      catch (elle::service::aws::AWSException const& e)
      {
        if (e.inner_exception())
        {
          try
          {
            std::rethrow_exception(e.inner_exception());
          }
          catch (elle::service::aws::FileNotFound const& e)
          {
            ELLE_WARN("unable to DELETE block: %s", e);
            throw MissingKey(key);
          }
        }
        else
        {
          throw e;
        }
      }
      return 0;
    }

    std::vector<Key>
    S3::_list()
    {
      std::vector<Key> res;
      auto s3_res = this->_storage->list_remote_folder_full();
      for (auto const& pair: s3_res)
      {
        try
        {
          res.push_back(
            infinit::model::Address::from_string(pair.first));
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("ignoring filename that is not an address: %s", pair.first);
        }
      }
      return res;
    }

    S3StorageConfig::S3StorageConfig(std::string name,
                                     elle::service::aws::Credentials credentials,
                                     elle::service::aws::S3::StorageClass storage_class,
                                     boost::optional<int64_t> capacity,
                                     boost::optional<std::string> description)
      : Super(
          std::move(name), std::move(capacity), std::move(description))
      , credentials(std::move(credentials))
      , storage_class(storage_class)
    {}

    S3StorageConfig::S3StorageConfig(elle::serialization::SerializerIn& s)
      : Super(s)
    {
      this->serialize(s);
    }

    void
    S3StorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("aws_credentials", this->credentials);
      if (s.out())
      {
        auto out = [this] () -> std::string {
          switch (this->storage_class)
          {
            case StorageClass::Standard:
              return "standard";
            case StorageClass::StandardIA:
              return "standard_ia";
            case StorageClass::ReducedRedundancy:
              return "reduced_redundancy";
            default:
              return "default";
          }
        }();
        s.serialize("storage_class", out);
      }
      else
      {
        try
        {
          std::string in;
          s.serialize("storage_class", in);
          if (in == "standard")
            this->storage_class = StorageClass::Standard;
          else if (in == "standard_ia")
            this->storage_class = StorageClass::StandardIA;
          else if (in == "reduced_redundancy")
            this->storage_class = StorageClass::ReducedRedundancy;
          else
            this->storage_class = StorageClass::Default;
        }
        catch (elle::serialization::MissingKey const& e)
        {
          bool reduced_redundancy;
          s.serialize("reduced_redundancy", reduced_redundancy);
          if (reduced_redundancy)
            this->storage_class = StorageClass::ReducedRedundancy;
          else
            this->storage_class = StorageClass::Default;
        }
      }
    }

    std::unique_ptr<infinit::silo::Storage>
    S3StorageConfig::make()
    {
      auto s3 = std::make_unique<elle::service::aws::S3>(credentials);
      return std::make_unique<infinit::silo::S3>(std::move(s3),
                                                     this->storage_class,
                                                     this->capacity);
    }
  }
}

namespace
{
  auto const res =
    elle::serialization::Hierarchy<infinit::silo::StorageConfig>::
    Register<infinit::silo::S3StorageConfig>("s3");
}
