#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <elle/bench.hh>
#include <elle/log.hh>

#include <elle/serialization/json.hh>
#include <elle/json/json.hh>

#include <infinit/silo/Collision.hh>
#include <infinit/silo/GCS.hh>
#include <infinit/silo/MissingKey.hh>

#include <sstream>
#include <string>

ELLE_LOG_COMPONENT("infinit.storage.GCS");


#define BENCH(name)                                       \
  static elle::Bench bench("bench.gcs." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

using StatusCode = elle::reactor::http::StatusCode;

namespace infinit
{
  namespace silo
  {
    std::string
    GCS::_url(Key key) const
    {
      return elle::sprintf("https://storage.googleapis.com/%s/%s/%x",
        this->_bucket, this->_root, key);
    }

    GCS::GCS(std::string const& name,
             std::string const& bucket,
             std::string const& root,
             std::string const& refresh_token)
      : GoogleAPI(name, refresh_token)
      , _bucket(bucket)
      , _root(root.empty() ? std::string(".infinit-storage") : root)
    {}

    elle::Buffer
    GCS::_get(Key key) const
    {
      BENCH("get");
      ELLE_DEBUG("get %x", key);
      std::string url = this->_url(key);
      auto r = this->_request(url,
                              elle::reactor::http::Method::GET,
                              elle::reactor::http::Request::QueryDict(),
                              {},
                              {StatusCode::Not_Found});
      if (r.status() == StatusCode::Not_Found)
        throw MissingKey(key);
      else if (r.status() == StatusCode::OK)
      {
        auto res = r.response();
        ELLE_TRACE("%s: got %s bytes for %x", *this, res.size(), key);
        return res;
      }
      else
        elle::unreachable();
    }

    int
    GCS::_set(Key key,
              elle::Buffer const& value,
              bool insert,
              bool update)
    {
      // FIXME: properly handle insert and update flags.
      BENCH("set");
      ELLE_DEBUG("set %x", key);
      if (!insert && !update)
        elle::err("neither inserting nor updating");
      std::string url = this->_url(key);
      auto r = this->_request(url,
                              elle::reactor::http::Method::PUT,
                              elle::reactor::http::Request::QueryDict(),
                              {},
                              {},
                              value);
      return value.size();
    }

    int
    GCS::_erase(Key k)
    {
      BENCH("erase");
      ELLE_DEBUG("erase %x", k);
      std::string url = this->_url(k);
      auto r = this->_request(url,
                              elle::reactor::http::Method::DELETE,
                              elle::reactor::http::Request::QueryDict(),
                              {},
                              {StatusCode::No_Content, StatusCode::Not_Found});
      return 0;
    }

    std::vector<Key>
    GCS::_list()
    {
      auto url = elle::sprintf("https://storage.googleapis.com/%s?prefix=%s",
                               this->_bucket, this->_root);
      auto res = std::vector<Key>{};
      while (true)
      {
        auto r = this->_request(url,
                                elle::reactor::http::Method::GET,
                                elle::reactor::http::Request::QueryDict());
        using boost::property_tree::ptree;
        ptree response;
        read_xml(r, response);
        for (auto const& base_element: response.get_child("ListBucketResult"))
          if (base_element.first == "Contents")
          {
             std::string fname = base_element.second.get<std::string>("Key");
             auto pos = fname.find("0x");
             res.emplace_back(Key::from_string(fname.substr(pos+2)));
          }
        try
        {
          auto next = response.get_child("ListBucketResult").get<std::string>("NextMarker");
          url = elle::sprintf(
            "https://storage.googleapis.com/%s?prefix=%s&marker=%s",
            this->_bucket, this->_root, next);
        }
        catch (std::exception const& e)
        {
          ELLE_TRACE("listing finished: %s", e.what());
          break;
        }
      }
      if (res.empty())
        ELLE_TRACE("listing is empty");
      else
        ELLE_TRACE("reloaded %s keys from %s to %s", res.size(),
          res.front(), res.back());
      return res;
    }

    GCSConfig::GCSConfig(std::string const& name,
                         std::string const& bucket,
                         std::string const& root,
                         std::string const& user_name,
                         std::string const& refresh_token,
                         boost::optional<int64_t> capacity,
                         boost::optional<std::string> description)
      : StorageConfig(name, std::move(capacity), std::move(description))
      , bucket(bucket)
      , root(root)
      , refresh_token(refresh_token)
      , user_name(user_name)
    {}

    GCSConfig::GCSConfig(elle::serialization::SerializerIn& s)
      : StorageConfig(s)
      , bucket(s.deserialize<std::string>("bucket"))
      , root(s.deserialize<std::string>("root"))
      , refresh_token(s.deserialize<std::string>("refresh_token"))
      , user_name(s.deserialize<std::string>("user_name"))
    {}

    void
    GCSConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("bucket", this->bucket);
      s.serialize("root", this->root);
      s.serialize("refresh_token", this->refresh_token);
      s.serialize("user_name", this->user_name);
    }

    std::unique_ptr<infinit::silo::Storage>
    GCSConfig::make()
    {
      return std::make_unique<infinit::silo::GCS>(
        this->user_name, this->bucket, this->root, this->refresh_token);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<GCSConfig> _register_GCSConfig("gcs");
  }
}
