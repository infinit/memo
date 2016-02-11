#include <fstream>

#include <infinit/storage/S3.hh>

#include <elle/log.hh>
#include <elle/bench.hh>
#include <elle/serialization/json/SerializerIn.hh>
#include <aws/S3.hh>

#include <infinit/model/Address.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <infinit/storage/MissingKey.hh>

ELLE_LOG_COMPONENT("infinit.storage.S3");

#define BENCH(name)                                      \
  static elle::Bench bench("bench.s3store." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

namespace infinit
{
  namespace storage
  {
    S3::S3(std::unique_ptr<aws::S3> storage,
           bool reduced_redundancy,
           boost::optional<int64_t> capacity)
      : Storage(std::move(capacity))
      , _storage(std::move(storage))
      , _reduced_redundancy(reduced_redundancy)
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
      catch (aws::AWSException const& e)
      {
        if (e.inner_exception())
        {
          try
          {
            std::rethrow_exception(e.inner_exception());
          }
          catch (aws::FileNotFound const& e)
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
      BENCH("set");
      if (!insert && !update)
        throw elle::Error("neither inserting nor updating");
      if (!insert || !update)
        throw elle::Error("only update and insert are supported");
      // FIXME: Use multipart upload for blocks bigger than 5 MiB.
      this->_storage->put_object(value,
                                 elle::sprintf("%x", key),
                                 aws::RequestQuery(),
                                 !this->reduced_redundancy());
      return 0;
    }

    int
    S3::_erase(Key key)
    {
      try
      {
        this->_storage->delete_object(elle::sprintf("%x", key));
      }
      catch (aws::AWSException const& e)
      {
        if (e.inner_exception())
        {
          try
          {
            std::rethrow_exception(e.inner_exception());
          }
          catch (aws::FileNotFound const& e)
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
            infinit::model::Address::from_string(pair.first.substr(2)));
        }
        catch (elle::Error const& e)
        {
          ELLE_WARN("ignoring filename that is not an address: %s", pair.first);
        }
      }
      return res;
    }

    S3StorageConfig::S3StorageConfig(std::string name,
                                    aws::Credentials credentials,
                                    bool reduced_redundancy,
                                    boost::optional<int64_t> capacity)
      : StorageConfig(std::move(name), std::move(capacity))
      , credentials(std::move(credentials))
      , reduced_redundancy(reduced_redundancy)
    {}

    S3StorageConfig::S3StorageConfig(elle::serialization::SerializerIn& input)
      : StorageConfig()
    {
      this->serialize(input);
    }

    void
    S3StorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("aws_credentials", this->credentials);
      s.serialize("reduced_redundancy", this->reduced_redundancy);
    }

    std::unique_ptr<infinit::storage::Storage>
    S3StorageConfig::make()
    {
      auto s3 = elle::make_unique<aws::S3>(credentials);
      return elle::make_unique<infinit::storage::S3>(std::move(s3),
                                                     this->reduced_redundancy,
                                                     this->capacity);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<S3StorageConfig>
    _register_S3StorageConfig("s3");
  }
}
