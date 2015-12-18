#ifndef INFINIT_CREDENTIALS_HH
# define INFINIT_CREDENTIALS_HH

# include <elle/serialization/Serializer.hh>

# include <infinit/serialization.hh>

namespace infinit
{
  class Credentials
    : public elle::serialization::VirtuallySerializable<false>
  {
  public:
    Credentials() = default;
    Credentials(elle::serialization::SerializerIn& input);
    virtual
    ~Credentials();

    virtual
    void
    serialize(elle::serialization::Serializer& s);
    typedef infinit::serialization_tag serialization_tag;
    static constexpr char const* virtually_serializable_key = "type";

    virtual
    std::string
    display_name() const;

    virtual
    std::string
    uid() const;
  };
}

#endif
