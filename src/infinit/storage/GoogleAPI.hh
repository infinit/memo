#ifndef INFINIT_STORAGE_GOOGLE_API_HH
# define INFINIT_STORAGE_GOOGLE_API_HH

# include <elle/reactor/http/Request.hh>

namespace infinit
{
  namespace storage
  {
    class GoogleAPI
    {
    protected:
      GoogleAPI(std::string const& name,
                std::string const& refresh_token);
      void
      _refresh();

      elle::reactor::http::Request
      _request(std::string url,
               elle::reactor::http::Method method,
               elle::reactor::http::Request::QueryDict query,
               elle::reactor::http::Request::Configuration conf
                 = elle::reactor::http::Request::Configuration{},
               std::vector<elle::reactor::http::StatusCode>
               = std::vector<elle::reactor::http::StatusCode>{},
               elle::Buffer const& payload = elle::Buffer()) const;
      ELLE_ATTRIBUTE_R(std::string, name, protected);
      ELLE_ATTRIBUTE_R(std::string, token, protected);
      ELLE_ATTRIBUTE_R(std::string, refresh_token);
    };
  }
}
#endif