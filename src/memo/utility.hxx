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
    _read_error(elle::json::Object& request)
    {
      auto error = boost::any_cast<std::string>(request["error"]);
      throw Exception(error);
    }

    template <>
    ELLE_COMPILER_ATTRIBUTE_NORETURN
    inline
    void
    _read_error<BeyondError>(elle::json::Object& request)
    {
      auto error = boost::any_cast<std::string>(request["error"]);
      auto reason = boost::any_cast<std::string>(request["reason"]);
      boost::optional<std::string> name = boost::none;
      if (request.find("name") != request.end())
        name = boost::any_cast<std::string>(request["name"]);
      throw BeyondError(error, reason, name);
    }
  }

  template <typename Exception>
  void
  read_error(elle::reactor::http::Request& r,
             std::string const& type,
             std::string const& name)
  {
    ELLE_LOG_COMPONENT("memo");
    ELLE_DEBUG("read_error");
    elle::json::Object json;
    try
    {
      json = boost::any_cast<elle::json::Object>(elle::json::read(r));
    }
    catch (boost::bad_any_cast const& e)
    {
      ELLE_DEBUG("Not json. html ?");
      throw elle::Error(e.what());
    }
    catch (elle::json::ParseError const& e)
    {
      ELLE_DEBUG("ParseError: %s", e.what());
      throw elle::Error(
        elle::sprintf("unexpected HTTP error %s while performing %s for %s %s",
                      r.status(), r.method(), type, name));
    }
    _details::_read_error<Exception>(json);
  }
}
