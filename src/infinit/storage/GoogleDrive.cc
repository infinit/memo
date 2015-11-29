#include <das/model.hh>
#include <das/serializer.hh>

#include <elle/log.hh>
#include <elle/os/environ.hh>
#include <elle/serialization/json.hh>
#include <elle/json/json.hh>

#include <infinit/storage/Collision.hh>
#include <infinit/storage/GoogleDrive.hh>
#include <infinit/storage/MissingKey.hh>

#include <sstream>
#include <string>

ELLE_LOG_COMPONENT("infinit.storage.GoogleDrive");

struct Parent
{
  std::string id;
};

DAS_MODEL(Parent, (id), DasParent);
DAS_MODEL_DEFAULT(Parent, DasParent);
DAS_MODEL_SERIALIZE(Parent);

struct Directory
{
  std::string title;
  std::vector<Parent> parents;
  std::string mimeType;
};

DAS_MODEL(Directory, (title, parents, mimeType), DasDirectory);
DAS_MODEL_DEFAULT(Directory, DasDirectory);
DAS_MODEL_SERIALIZE(Directory);

struct Metadata
{
  std::string title;
  std::vector<Parent> parents;
};

DAS_MODEL(Metadata, (title, parents), DasMetadata);
DAS_MODEL_DEFAULT(Metadata, DasMetadata);
DAS_MODEL_SERIALIZE(Metadata);


namespace infinit
{
  namespace storage
  {
    std::string
    beyond()
    {
      auto static const res = elle::os::getenv("INFINIT_BEYOND", "${beyond_host}");
      return res;
    }

    /*
     * GoogleDrive
     */

    static reactor::Duration delay(int attempt)
    {
      if (attempt > 8)
        attempt = 8;
      unsigned int factor = pow(2, attempt);
      return boost::posix_time::milliseconds(factor * 100);
    }

    boost::filesystem::path
    GoogleDrive::_path(Key key) const
    {
      return this->_root / elle::sprintf("%x", key);
    }

    GoogleDrive::GoogleDrive(std::string refresh_token,
                             std::string name)
      : GoogleDrive{".infinit",
                    std::move(refresh_token),
                    std::move(name)}
    {}

    GoogleDrive::GoogleDrive(boost::filesystem::path root,
                             std::string refresh_token,
                             std::string name)
      : _token{"unset_access_token"}
      , _root{std::move(root)}
      , _refresh_token{refresh_token}
      , _name{name}
    {
      std::string id = this->_exists(this->_root.string());
      if (id == "")
      {
        auto r = this->_mkdir(this->_root.string());
        auto json = boost::any_cast<elle::json::Object>(elle::json::read(r));
        id = boost::any_cast<std::string>(json["id"]);
      }

      this->_dir_id = id;
    }

    elle::Buffer
    GoogleDrive::_get(Key key) const
    {
      ELLE_DEBUG("get %x", key);

      using StatusCode = reactor::http::StatusCode;
      std::string id = _exists(elle::sprintf("%x", key));
      if (id == "")
        throw MissingKey(key);

      auto url = elle::sprintf("https://www.googleapis.com/drive/v2/files/%s",
                               id);
      auto conf = reactor::http::Request::Configuration();
      auto r = this->_request(url,
                              reactor::http::Method::GET,
                              reactor::http::Request::QueryDict{{"alt", "media"}},
                              conf,
                              {StatusCode::Not_Found});

      if (r.status() == StatusCode::Not_Found)
        throw MissingKey(key);
      else if (r.status() == StatusCode::OK)
      {
        auto res = r.response();
        ELLE_TRACE("%s: got %s bytes for %x", *this, res.size(), key);
        return res;
      }

      ELLE_TRACE("The impossible happened: %s", r.status());
      elle::unreachable();
    }

    int
    GoogleDrive::_set(Key key,
                      elle::Buffer const& value,
                      bool insert,
                      bool update)
    {
      using StatusCode = reactor::http::StatusCode;
      ELLE_DEBUG("set %x", key);
      if (insert)
      {
        try
        {
          this->_erase(key);
          ELLE_DUMP("replace");
        }
        catch(MissingKey const& e)
        {
          ELLE_DUMP("new");
        }

        auto r = this->_insert(key, value);
        if (r.status() == StatusCode::Not_Found && !update)
          throw Collision(key);
      }
      else if (update)
      {
        // Non-sense. If a file exists->remove it first then write.
      }
      else
        throw elle::Error("neither inserting, nor updating");

      // FIXME: impl.
      return 0;
    }

    int
    GoogleDrive::_erase(Key k)
    {
      ELLE_DUMP("_erase");
      using Request = reactor::http::Request;
      using Method = reactor::http::Method;
      using StatusCode = reactor::http::StatusCode;

      Request::Configuration conf;

      std::string id = this->_exists(elle::sprintf("%x", k));
      if (id == "")
        throw MissingKey(k);

      auto url = elle::sprintf("https://www.googleapis.com/drive/v2/files/%s",
                               id);
      auto r = this->_request(url,
                              Method::DELETE,
                              Request::QueryDict{},
                              conf,
                              std::vector<StatusCode>{StatusCode::Not_Found,
                                                      StatusCode::No_Content});

      if (r.status() == StatusCode::Not_Found)
        throw elle::Error(elle::sprintf("File %s not found", id));

      // FIXME: impl.
      return 0;
    }

    std::vector<Key>
    GoogleDrive::_list()
    {
      ELLE_DUMP("_list (Not used)");
      throw elle::Error("Not implemented yet.");
    }

    BlockStatus
    GoogleDrive::_status(Key k)
    {
      std::string id = this->_exists(elle::sprintf("%x", k));

      if (id == "")
        return BlockStatus::missing;
      else
        return BlockStatus::exists;
    }

    reactor::http::Request
    GoogleDrive::_request(std::string url,
                          reactor::http::Method method,
                          reactor::http::Request::QueryDict query,
                          reactor::http::Request::Configuration conf,
                          std::vector<reactor::http::StatusCode> expected_codes) const
    {
      ELLE_DUMP("_request %s", method);
      using Request = reactor::http::Request;
      using StatusCode = reactor::http::StatusCode;

      expected_codes.push_back(StatusCode::OK);
      unsigned attempt = 0;

      conf.timeout(reactor::DurationOpt());
      conf.header_add("Authorization", elle::sprintf("Bearer %s", this->_token));

      while (true)
      {
        Request r{url, method, conf};
        r.query_string(query);
        r.finalize();

        if (std::find(expected_codes.begin(), expected_codes.end(), r.status())
            != expected_codes.end())
          return r;
        else if (r.status() == StatusCode::Forbidden
                 || r.status() == StatusCode::Unauthorized)
          const_cast<GoogleDrive*>(this)->_refresh();

        ELLE_WARN("Unexpected google HTTP response: %s, attempt %s",
                  r.status(),
                  attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        ++attempt;
        reactor::sleep(delay(attempt));
      }
    }

    reactor::http::Request
    GoogleDrive::_mkdir(std::string const& path) const
    {
      ELLE_DUMP("_mkdir");
      using Request = reactor::http::Request;
      using Method = reactor::http::Method;
      using Configuration = reactor::http::Request::Configuration;
      using StatusCode = reactor::http::StatusCode;

      Configuration conf;
      conf.timeout(reactor::DurationOpt());

      Directory dir{path,
                    {Parent{"root"}},
                    "application/vnd.google-apps.folder"};

      unsigned attempt = 0;
      while (true)
      {
        conf.header_add("Authorization", elle::sprintf("Bearer %s", this->_token));
        Request r("https://www.googleapis.com/drive/v2/files",
                  Method::POST,
                  "application/json",
                  conf);

        elle::serialization::json::serialize(dir, r, false);
        r.finalize();

        if (r.status() == StatusCode::OK)
          return r;

        ELLE_WARN("Unexpected google HTTP status on POST: %s, attempt %s",
                  r.status(),
                  attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        std::cout << r;

        reactor::sleep(delay(attempt++));
      }
    }

    reactor::http::Request
    GoogleDrive::_insert(Key key, elle::Buffer const& value) const
    {
      ELLE_DUMP("_insert");
      using Configuration = reactor::http::Request::Configuration;
      using Method = reactor::http::Method;
      using Request = reactor::http::Request;
      using StatusCode = reactor::http::StatusCode;

      Configuration conf;
      conf.timeout(reactor::DurationOpt());
      unsigned attempt = 0;

      // https://developers.google.com/drive/web/manage-uploads#multipart

      Request::QueryDict query;
      query["uploadType"] = "multipart";

      std::string delim_value = "galibobro";
      std::string delim = "--" + delim_value;
      std::string mime_meta = "Content-Type: application/json; charset=UTF-8";
      std::string mime = "Content-Type: application/octet-stream";

      conf.header_add("Content-Type",
                      elle::sprintf("multipart/related; boundary=\"%s\"",
                                    delim_value));


      Metadata metadata{elle::sprintf("%x", key), {Parent{this->_dir_id}}};

      while (true)
      {
        conf.header_add("Authorization",
                        elle::sprintf("Bearer %s", this->_token));
        Request r{"https://www.googleapis.com/upload/drive/v2/files",
                  Method::POST,
                  conf};
        r.query_string(query);

        r.write(delim.c_str(), delim.size());
        r.write("\n", 1);
        r.write(mime_meta.c_str(), mime_meta.size());
        r.write("\n\n", 2);
        elle::serialization::json::serialize(metadata, r, false);
        r.write("\n\n", 2);
        r.write(delim.c_str(), delim.size());
        r.write("\n", 1);
        r.write(mime.c_str(), mime.size());
        r.write("\n\n", 2);
        r.write(value.string().c_str(), value.size());
        r.write("\n\n", 2);
        r.write(delim.c_str(), delim.size());
        r.write("--", 2);

        r.finalize();

        if (r.status() == StatusCode::OK)
          return r;
        else if (r.status() == StatusCode::Forbidden
                 || r.status() == StatusCode::Unauthorized)
        {
          const_cast<GoogleDrive*>(this)->_refresh();
        };

        ELLE_WARN("Unexpected google HTTP status (insert): %s, attempt %s",
            r.status(),
            attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        reactor::sleep(delay(attempt++));
      }
    }

    void
    GoogleDrive::_refresh()
    {
      ELLE_DUMP("_refresh");
      using Configuration = reactor::http::Request::Configuration;
      using Method = reactor::http::Method;
      using Request = reactor::http::Request;
      using StatusCode = reactor::http::StatusCode;

      Configuration conf;
      conf.timeout(reactor::DurationOpt());
      unsigned attempt = 0;

      reactor::http::Request::QueryDict query{
        {"refresh_token", this->_refresh_token}};

      while (true)
      {
        auto url = elle::sprintf("%s/users/%s/credentials/google/refresh",
                                beyond(),
                                this->_name);
        Request r{url,
                  Method::GET,
                  conf};
        r.query_string(query);
        r.finalize();

        if (r.status() == StatusCode::OK)
        {
          this->_token = r.response().string();
          // FIXME: Update the conf file. Credentials or storage or both ?
          break;
        }

        ELLE_WARN("Unexpected google HTTP status (refresh): %s, attempt %s",
                  r.status(),
                  attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        reactor::sleep(delay(attempt++));
      }
    }

    std::string
    GoogleDrive::_exists(std::string file_name) const
    {
      ELLE_DUMP("_exists");
      using Configuration = reactor::http::Request::Configuration;
      using Method = reactor::http::Method;
      using Request = reactor::http::Request;
      using StatusCode = reactor::http::StatusCode;

      Configuration conf;
      conf.timeout(reactor::DurationOpt());
      unsigned attempt = 0;
      reactor::http::Request::QueryDict query;

      while (true)
      {
        query["access_token"] = this->_token;
        query["q"] = elle::sprintf("title = '%s' and trashed = false",
                                   file_name);
        Request r{"https://www.googleapis.com/drive/v2/files/",
                  Method::GET,
                  conf};
        r.query_string(query);

        r.finalize();

        if (r.status() == StatusCode::OK)
        {
          auto json = boost::any_cast<elle::json::Object>(
              elle::json::read(r));
          auto items = boost::any_cast<elle::json::Array>(json["items"]);
          for (auto const& item: items)
          {
            auto f = boost::any_cast<elle::json::Object>(item);
            std::string id = boost::any_cast<std::string>(f["id"]);
            return id;
          }
          return "";
        }
        else if (r.status() == StatusCode::Unauthorized
                 || r.status() == StatusCode::Forbidden)
          const_cast<GoogleDrive*>(this)->_refresh();
        else
        {
          ELLE_WARN("Unexpected google HTTP status (check): %s, attempt %s",
              r.status(),
              attempt + 1);
          ELLE_DUMP("body: %s", r.response());
        }

        reactor::sleep(delay(attempt++));
      }
    }

    /*
     *  GoogleDriveStorageConfig
     */

    GoogleDriveStorageConfig::GoogleDriveStorageConfig(
        std::string name,
        boost::optional<std::string> root_,
        std::string refresh_token_,
        std::string user_name_,
        boost::optional<int64_t> capacity)
      : StorageConfig(std::move(name), std::move(capacity))
      , root{std::move(root_)}
      , refresh_token{std::move(refresh_token_)}
      , user_name{std::move(user_name_)}
    {}

    GoogleDriveStorageConfig::GoogleDriveStorageConfig(
        elle::serialization::SerializerIn& input)
      : StorageConfig()
    {
      this->serialize(input);
    }

    void
    GoogleDriveStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("root", this->root);
      s.serialize("refresh_token", this->refresh_token);
      s.serialize("user_name", this->user_name);
    }

    std::unique_ptr<infinit::storage::Storage>
    GoogleDriveStorageConfig::make()
    {
      if (this->root)
        return elle::make_unique<infinit::storage::GoogleDrive>(
            this->root.get(), this->refresh_token, this->user_name);
      else
        return elle::make_unique<infinit::storage::GoogleDrive>(
            this->refresh_token, this->user_name);
    }

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<GoogleDriveStorageConfig> _register_GoogleDriveStorageConfig(
      "google");
  }
}
