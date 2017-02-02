#pragma once

#include <infinit/Network.hh>
#include <infinit/User.hh>
#include <infinit/Volume.hh>
#include <infinit/descriptor/TemplatedBaseDescriptor.hh>

namespace infinit
{
  struct Drive
    : public descriptor::TemplatedBaseDescriptor<Drive>
  {
    struct User
    {
      User() = default;
      User(std::string const& permissions,
           std::string const& status,
           bool create_home);
      void
      serialize(elle::serialization::Serializer& s);
      bool
      operator==(User const& other);

      std::string permissions = "rw";
      std::string status;
      bool create_home;
    };

    using Users = std::unordered_map<std::string, User>;

  private:
    Drive(std::string const& name,
          std::string const& owner,
          std::string const& volume,
          std::string const& network,
          boost::optional<std::string> description,
          Users const& users);

  public:
    Drive(std::string const& name,
          infinit::User const& owner,
          Volume const& volume,
          Network const& network,
          boost::optional<std::string> description,
          Users const& users);
    Drive(elle::serialization::SerializerIn& s);
    void
    serialize(elle::serialization::Serializer& s) override;
    void
    print(std::ostream& out) const override;

    std::string owner;
    std::string volume;
    std::string network;
    Users users;
    boost::optional<std::string> icon_path;
  };
}
