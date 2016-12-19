#include <infinit/Drive.hh>

namespace infinit
{
  Drive::User::User(std::string const& permissions,
                    std::string const& status,
                    bool create_home)
    : permissions(permissions)
    , status(status)
    , create_home(create_home)
  {}

  void
  Drive::User::serialize(elle::serialization::Serializer& s)
  {
    s.serialize("permissions", this->permissions);
    s.serialize("status", this->status);
    s.serialize("create_home", this->create_home);
  }

  bool
  Drive::User::operator==(User const& other)
  {
    return permissions == other.permissions
      && status == other.status
      && create_home == other.create_home;
  }

  Drive::Drive(std::string const& name,
          std::string const& owner,
          std::string const& volume,
          std::string const& network,
          boost::optional<std::string> description,
          Users const& users)
    : descriptor::TemplatedBaseDescriptor<Drive>(name, std::move(description))
    , owner(owner)
    , volume(volume)
    , network(network)
    , users(users)
  {}

  Drive::Drive(std::string const& name,
               infinit::User const& owner,
               Volume const& volume,
               Network const& network,
               boost::optional<std::string> description,
               Users const& users)
    : Drive(name, owner.name, volume.name, network.name,
            std::move(description), users)
  {
    if (this->users.find(owner.name) == this->users.end())
      this->users[owner.name] = User{"rw", "ok", false};
  }

  Drive::Drive(elle::serialization::SerializerIn& s)
    : descriptor::TemplatedBaseDescriptor<Drive>(s)
    , owner(s.deserialize<std::string>("owner"))
    , volume(s.deserialize<std::string>("volume"))
    , network(s.deserialize<std::string>("network"))
    , users(s.deserialize<Users>("users"))
    , icon_path(s.deserialize<boost::optional<std::string>>("icon_path"))
  {}

  void
  Drive::serialize(elle::serialization::Serializer& s)
  {
    descriptor::TemplatedBaseDescriptor<Drive>::serialize(s);
    s.serialize("owner", this->owner);
    s.serialize("volume", this->volume);
    s.serialize("network", this->network);
    s.serialize("users", this->users);
    s.serialize("icon_path", this->icon_path);
  }

  void
  Drive::print(std::ostream& out) const
  {
    out << "Drive(" << this->name << ")";
  }
}
