#pragma once

#include <elle/serialization/Serializer.hh>

#include <memo/serialization.hh>

namespace memo
{
  class Credentials
    : public elle::serialization::VirtuallySerializable<Credentials, false>
  {
  public:
    Credentials() = default;
    Credentials(elle::serialization::SerializerIn& input);
    virtual
    ~Credentials() = default;

    virtual
    void
    serialize(elle::serialization::Serializer& s);
    using serialization_tag = memo::serialization_tag;
    static constexpr char const* virtually_serializable_key = "type";

    virtual
    std::string
    display_name() const = 0;

    virtual
    std::string
    uid() const = 0;
  };
}
