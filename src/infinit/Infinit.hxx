#pragma once

namespace infinit
{
  template <typename T>
  std::vector<std::unique_ptr<T, std::default_delete<Credentials>>>
  Infinit::credentials(std::string const& name) const
  {
    std::vector<
      std::unique_ptr<
        T,
        std::default_delete<Credentials>
        >
      > res;
    auto const path = this->_credentials_path(name);
    boost::filesystem::directory_iterator const end;
    for (boost::filesystem::directory_iterator it(path);
         it != end;
         ++it)
    {
      if (is_regular_file(it->status()))
      {
        boost::filesystem::ifstream f;
        this->_open_read(f, it->path(), name, "credential");
        res.push_back(std::dynamic_pointer_cast<T>(load<std::unique_ptr<Credentials>>(f)));
      }
    }
    return res;
  }

  template <typename T>
  T
  Infinit::load(std::ifstream& input)
  {
    ELLE_ASSERT(input.is_open());
    return elle::serialization::json::deserialize<T>(
      input,
      false /* versioned */
      );
  }

  template <typename T>
  void
  Infinit::save(std::ostream& output, T const& resource, bool pretty)
  {
    ELLE_ASSERT(output.good());
    elle::serialization::json::serialize(
      resource, output,
      false, /* versioned */
      pretty);
  }

  // Beyond

  template <typename T>
  T
  Infinit::beyond_fetch(std::string const& where,
                        std::string const& type,
                        std::string const& name,
                        boost::optional<infinit::User const&> self,
                        infinit::Headers const& extra_headers) const
  {
    auto json = beyond_fetch_json(where, type, name, self, extra_headers);
    elle::serialization::json::SerializerIn input(json, false);
    this->report_local_action()("fetched", type, name);
    return input.deserialize<T>();
  }

  template <typename T>
  T
  Infinit::beyond_fetch(std::string const& type,
                        std::string const& name) const
  {
    return beyond_fetch<T>(elle::sprintf("%ss/%s", type, name), type, name);
  }

  template <typename Serializer, typename T>
  void
  Infinit::beyond_push(std::string const& where,
                       std::string const& type,
                       std::string const& name,
                       T const& o,
                       infinit::User const& self,
                       bool beyond_error,
                       bool update) const
  {
    ELLE_LOG_COMPONENT("infinit");
    auto payload_ = [&] {
      std::stringstream stream;
      elle::serialization::json::serialize<Serializer>(o, stream, false);
      return stream.str();
    }();
    ELLE_TRACE("pushing %s/%s with payload %s", type, name, payload_);
    elle::ConstWeakBuffer payload{payload_.data(), payload_.size()};
    beyond_push_data(
      where, type, name, payload, "application/json", self, beyond_error,
      update);
  }

  template <typename Serializer, typename T>
  void
  Infinit::beyond_push(std::string const& type,
                       std::string const& name,
                       T const& o,
                       infinit::User const& self,
                       bool beyond_error,
                       bool update) const
  {
    beyond_push<Serializer>(
      elle::sprintf("%ss/%s", type, name),
      type, name, o, self, beyond_error, update);
  }
}
