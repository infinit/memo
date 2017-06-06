#include <infinit/utility.hh>

#include <regex>

#include <elle/format/base64.hh>
#include <elle/json/exceptions.hh>

#include <elle/cryptography/hash.hh>
#include <elle/cryptography/rsa/KeyPair.hh>

#include <elle/reactor/scheduler.hh>

#include <infinit/version.hh>

ELLE_LOG_COMPONENT("infinit.utility");

namespace infinit
{
  elle::Version
  version()
  {
    return {INFINIT_MAJOR, INFINIT_MINOR, INFINIT_SUBMINOR};
  }

  std::string
  version_describe()
  {
    return INFINIT_VERSION;
  }

  BeyondError::BeyondError(std::string const& error,
                           std::string const& reason,
                           boost::optional<std::string> const& name)
    : elle::Error(reason)
    , _error(error)
    , _name(name)
  {
    ELLE_TRACE("error: %s", *this);
  }

  BeyondError::BeyondError(elle::serialization::SerializerIn& s)
    : BeyondError(s.deserialize<std::string>("error"),
                  s.deserialize<std::string>("reason"),
                  s.deserialize<boost::optional<std::string>>("name"))
  {}

  std::string
  BeyondError::name_opt() const
  {
    return _name.value_or("NAME");
  }

  Headers
  signature_headers(elle::reactor::http::Method method,
                    std::string const& where,
                    User const& self,
                    boost::optional<elle::ConstWeakBuffer> payload)
  {
    if (!self.private_key)
      elle::err("no private key for %s, unable to sign request", self.name);
    using namespace elle::cryptography;
    auto semi_colon_append = [](elle::Buffer& buffer, std::string const& str)
      {
        std::string res = elle::sprintf("%s;", str);
        return buffer.append(res.data(), res.size());
      };
    elle::Buffer string_to_sign = elle::Buffer();
    semi_colon_append(string_to_sign, elle::sprintf("%s", method));
    semi_colon_append(string_to_sign, where);
    auto payload_hash
      = payload
      ? hash(payload.get(), Oneway::sha256)
      : hash(elle::ConstWeakBuffer(), Oneway::sha256);
    auto encoded_hash = elle::format::base64::encode(payload_hash);
    semi_colon_append(string_to_sign, encoded_hash.string());
    auto now = std::to_string(time(0));
    string_to_sign.append(now.data(), now.size());
    auto signature = self.private_key->sign(
      string_to_sign,
      elle::cryptography::rsa::Padding::pkcs1,
      elle::cryptography::Oneway::sha256);
    auto encoded_signature = elle::format::base64::encode(signature);
    return {
      { "infinit-signature", encoded_signature.string() },
      { "infinit-time", now },
      { "infinit-user", self.name },
    };
  }

  std::string
  beyond(bool help)
  {
    // FIXME: no static here, tests/kelips.cc changes this environment
    // variable. We should change the test and fix this.
    auto const res = elle::os::getenv("INFINIT_BEYOND", BEYOND_HOST);
    if (help && res == BEYOND_HOST)
      return "the Hub";
    else
      return res;
  }

  std::string
  beyond_delegate_user()
  {
    static auto const res = std::string{BEYOND_DELEGATE_USER};
    return res;
  }

  bool
  is_hidden_file(boost::filesystem::path const& path)
  {
    return
      path.filename().string().front() == '.'
      || path.filename().string().back() == '~';
  }

  bool
  is_visible_file(boost::filesystem::directory_entry const& e)
  {
    return is_regular_file(e.status()) && !is_hidden_file(e);
  }

  bool
  validate_email(std::string const& candidate)
  {
    static const auto email_regex = std::regex(EMAIL_REGEX);
    return std::regex_match(candidate, email_regex);
  }
}
