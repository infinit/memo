#include <infinit/storage/GoogleAPI.hh>

#include <elle/bench.hh>
#include <elle/os/environ.hh>

#include <reactor/scheduler.hh>
#include <elle/log.hh>

ELLE_LOG_COMPONENT("infinit.storage.GoogleAPI");

#define BENCH(name)                                      \
  static elle::Bench bench("bench.googleapi." name, 10000_sec); \
  elle::Bench::BenchScope bs(bench)

namespace infinit
{
  namespace storage
  {
    static
    std::string
    beyond()
    {
      auto static const res = elle::os::getenv("INFINIT_BEYOND", "https://beyond.infinit.io");
      return res;
    }

    static reactor::Duration delay(int attempt)
    {
      if (attempt > 8)
        attempt = 8;
      unsigned int factor = pow(2, attempt);
      return boost::posix_time::milliseconds(factor * 100);
    }

    GoogleAPI::GoogleAPI(std::string const& name,
                         std::string const& refresh_token)
      : _name(name)
      , _token()
      , _refresh_token(refresh_token)
      {}

    reactor::http::Request
    GoogleAPI::_request(std::string url,
                          reactor::http::Method method,
                          reactor::http::Request::QueryDict query,
                          reactor::http::Request::Configuration conf,
                          std::vector<reactor::http::StatusCode> expected_codes,
                          elle::Buffer const& payload) const
    {
      ELLE_DUMP("_request %s", method);
      using Request = reactor::http::Request;
      using StatusCode = reactor::http::StatusCode;

      expected_codes.push_back(StatusCode::OK);
      unsigned attempt = 0;

      conf.timeout(reactor::DurationOpt());
      conf.header_add("Authorization", elle::sprintf("Bearer %s", this->_token));

      if (this->_token.empty())
        const_cast<GoogleAPI*>(this)->_refresh();
      while (true)
      {
        conf.header_remove("Authorization");
        conf.header_add("Authorization", elle::sprintf("Bearer %s", this->_token));

        Request r{url, method, conf};
        {
          BENCH("query");
          r.query_string(query);
          if (!payload.empty())
            r.write((const char*)payload.contents(), payload.size());
          r.finalize();
          r.status();
        }

        if (std::find(expected_codes.begin(), expected_codes.end(), r.status())
            != expected_codes.end())
          return r;
        else if (r.status() == StatusCode::Forbidden
                 || r.status() == StatusCode::Unauthorized)
          const_cast<GoogleAPI*>(this)->_refresh();

        ELLE_WARN("Unexpected google HTTP response on %s %s payload %s: %s, attempt %s",
                  method,
                  url,
                  payload.size(),
                  r.status(),
                  attempt + 1);
        ELLE_DUMP("body: %s", r.response());
        ++attempt;
        reactor::sleep(delay(attempt));
      }
    }
    void
    GoogleAPI::_refresh()
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
  }
}