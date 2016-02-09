#include <infinit/storage/InsufficientSpace.hh>

#include <elle/printf.hh>

namespace infinit
{
  namespace storage
  {
    namespace
    {
      static std::string __error_format =
        "Insufficient space in %s. %s/%s usage/capacity. Try to add %s Bytes.";
    }

    InsufficientSpace::InsufficientSpace(int delta,
                                         int64_t usage,
                                         int64_t capacity)
      : Super(elle::sprintf(__error_format,
                            "STORAGE_NAME",
                            usage,
                            capacity,
                            delta))
      , _delta{delta}
      , _usage{usage}
      , _capacity{capacity}
    {}

    InsufficientSpace::InsufficientSpace(elle::serialization::SerializerIn& in)
      : Super(in)
    {
      in.serialize("delta", _delta);
      in.serialize("usage", _usage);
      in.serialize("capacity", _capacity);
    }

    void
    InsufficientSpace::serialize(elle::serialization::Serializer& s,
                                 elle::Version const& v)
    {
      Super::serialize(s, v);
      s.serialize("delta", _delta);
      s.serialize("usage", _usage);
      s.serialize("capacity", _capacity);
    }

    static const elle::serialization::Hierarchy<elle::Exception>::
    Register<InsufficientSpace> _register_serialization;
  }
}
