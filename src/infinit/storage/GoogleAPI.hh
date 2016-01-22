#ifndef INFINIT_STORAGE_GOOGLE_API_HH
# define INFINIT_STORAGE_GOOGLE_API_HH

# include <reactor/http/Request.hh>

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

      reactor::http::Request
      _request(std::string url,
               reactor::http::Method method,
               reactor::http::Request::QueryDict query,
               reactor::http::Request::Configuration conf
                 = reactor::http::Request::Configuration{},
               std::vector<reactor::http::StatusCode>
               = std::vector<reactor::http::StatusCode>{},
               elle::Buffer const& payload = elle::Buffer()) const;
      ELLE_ATTRIBUTE_R(std::string, name, protected);
      ELLE_ATTRIBUTE_R(std::string, token, protected);
      ELLE_ATTRIBUTE_R(std::string, refresh_token);
    };
  }
}
#endif