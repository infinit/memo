#include <infinit/KeyValueStore.hh>

ELLE_LOG_COMPONENT("infinit");

namespace infinit
{
  KeyValueStore::KeyValueStore(descriptor::BaseDescriptor::Name name,
                               std::string network,
                               boost::optional<std::string> description)
    : descriptor::TemplatedBaseDescriptor<KeyValueStore>(
        std::move(name), std::move(description))
    , network(std::move(network))
  {}

  KeyValueStore::KeyValueStore(elle::serialization::SerializerIn& s)
    : descriptor::TemplatedBaseDescriptor<KeyValueStore>(s)
  {
    this->serialize(s);
  }

  void
  KeyValueStore::serialize(elle::serialization::Serializer& s)
  {
    descriptor::TemplatedBaseDescriptor<KeyValueStore>::serialize(s);
    s.serialize("network", this->network);
  }

  void
  KeyValueStore::print(std::ostream& out) const
  {
    out << "Volume(" << this->name << ")";
  }
}
