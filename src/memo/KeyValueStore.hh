#pragma once

#include <memo/model/doughnut/Doughnut.hh>

namespace memo
{
  struct KeyValueStore
    : public descriptor::TemplatedBaseDescriptor<KeyValueStore>
  {
    KeyValueStore(descriptor::BaseDescriptor::Name name,
                  std::string network,
                  boost::optional<std::string> description);

    KeyValueStore(elle::serialization::SerializerIn& s);

    void
    serialize(elle::serialization::Serializer& s) override;

    void
    print(std::ostream& out) const override;

    std::string network;
    using serialization_tag = memo::serialization_tag;
  };
}
