#pragma once

#include <elle/json/exceptions.hh>
#include <elle/json/json.hh>

namespace memo
{
  namespace _details
  {
    template <typename Exception>
    ELLE_COMPILER_ATTRIBUTE_NORETURN
    void
    _read_error(elle::json::Json const& request)
    {
      throw Exception(request["error"]);
    }

    template <>
    ELLE_COMPILER_ATTRIBUTE_NORETURN
    inline
    void
    _read_error<BeyondError>(elle::json::Json const& request)
    {
      boost::optional<std::string> name = boost::none;
      if (request.find("name") != request.end())
        name.emplace(request["name"]);
      throw BeyondError(request["error"], request["reason"], name);
    }
  }

  template <typename Exception>
  void
  read_error(elle::reactor::http::Request& r,
             std::string const& type,
             std::string const& name)
  {
    ELLE_LOG_COMPONENT("memo");
    ELLE_DEBUG_SCOPE("read_error");
    try
    {
      _details::_read_error<Exception>(elle::json::read(r));
    }
    catch (elle::json::ParseError const& e)
    {
      ELLE_DEBUG("ParseError: %s", e.what());
      throw elle::Error(
        elle::sprintf("unexpected HTTP error %s while performing %s for %s %s",
                      r.status(), r.method(), type, name));
    }
  }
}
