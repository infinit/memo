#include <memo/User.hh>

#include <elle/cryptography/hash.hh>

#include <elle/format/base64url.hh>

namespace memo
{
  User::User(std::string const& name,
             elle::cryptography::rsa::KeyPair const& keys,
             boost::optional<std::string> email,
             boost::optional<std::string> fullname,
             boost::optional<std::string> ldap_dn,
             boost::optional<std::string> description)
    : descriptor::TemplatedBaseDescriptor<User>(name, std::move(description))
    , public_key(keys.K())
    , private_key(keys.k())
    , email(email)
    , fullname(fullname)
    , password_hash()
    , ldap_dn(ldap_dn)
  {}

  bool
  User::permit_name_slash()
  {
    return false;
  }

  User::User(elle::serialization::SerializerIn& s)
    : descriptor::TemplatedBaseDescriptor<User>(s)
    , public_key(s.deserialize<elle::cryptography::rsa::PublicKey>("public_key"))
    , private_key(s.deserialize<boost::optional<
                  elle::cryptography::rsa::PrivateKey>>("private_key"))
    , email(s.deserialize<boost::optional<std::string>>("email"))
    , fullname(s.deserialize<boost::optional<std::string>>("fullname"))
    , password_hash()
    , ldap_dn(s.deserialize<boost::optional<std::string>>("ldap_dn"))
  {}

  elle::cryptography::rsa::KeyPair
  User::keypair() const
  {
    if (!this->private_key)
      elle::err("user %s has no private key", this->name);
    return elle::cryptography::rsa::KeyPair(this->public_key,
                                               this->private_key.get());
  }

  void
  User::serialize(elle::serialization::Serializer& s)
  {
    descriptor::TemplatedBaseDescriptor<User>::serialize(s);
    s.serialize("email", this->email);
    s.serialize("fullname", this->fullname);
    s.serialize("avatar", this->avatar_path);
    s.serialize("password_hash", this->password_hash);
    s.serialize("private_key", this->private_key);
    s.serialize("public_key", this->public_key);
    s.serialize("ldap_dn", this->ldap_dn);
  }

  std::string
  User::uid(elle::cryptography::rsa::PublicKey const& key)
  {
    auto serial = elle::cryptography::rsa::publickey::der::encode(key);
    auto hash = elle::cryptography::hash(serial, elle::cryptography::Oneway::sha256);
    return elle::format::base64url::encode(hash).string().substr(0, 8);
  }

  std::string
  User::uid() const
  {
    return uid(this->public_key);
  }

  bool
  User::operator ==(User const& user) const
  {
    return this->name == user.name &&
      this->public_key == user.public_key;
  }

  void
  User::print(std::ostream& out) const
  {
    out << "User(" << this->name << ": public";
    if (this->private_key)
      out << "/private keys";
    else
      out << " key only";
    out << ")";
  }
}
