#pragma once

#include <elle/reactor/http/Request.hh>

namespace infinit
{
  namespace storage
  {
    class GoogleAPI
    {
    protected:
      using Method = elle::reactor::http::Method;
      using Request = elle::reactor::http::Request;
      using StatusCode = elle::reactor::http::StatusCode;

      GoogleAPI(std::string name,
                std::string refresh_token);
      void
      _refresh();

      Request
      _request(std::string url,
               Method method,
               Request::QueryDict query,
               Request::Configuration conf = {},
               std::vector<StatusCode> = {},
               elle::Buffer const& payload = {}) const;
      ELLE_ATTRIBUTE_R(std::string, name, protected);
      ELLE_ATTRIBUTE_R(std::string, token, protected);
      ELLE_ATTRIBUTE_R(std::string, refresh_token);
    };
  }
}
