#pragma once

#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/descriptor/TemplatedBaseDescriptor.hh>

namespace memo
{
  struct User
    : public descriptor::TemplatedBaseDescriptor<User>
  {
    User(std::string const& name,
         elle::cryptography::rsa::KeyPair const& keys,
         boost::optional<std::string> email = {},
         boost::optional<std::string> fullname = {},
         boost::optional<std::string> ldap_dn = {},
         boost::optional<std::string> description = {});
    static
    bool
    permit_name_slash();
    User(elle::serialization::SerializerIn& s);
    elle::cryptography::rsa::KeyPair
    keypair() const;
    void
    serialize(elle::serialization::Serializer& s) override;
    static
    std::string
    uid(elle::cryptography::rsa::PublicKey const& key);
    std::string
    uid() const;
    bool
    operator ==(User const& user) const;
    void
    print(std::ostream& out) const override;
    elle::cryptography::rsa::PublicKey public_key;
    boost::optional<elle::cryptography::rsa::PrivateKey> private_key;
    // Hub.
    boost::optional<std::string> email;
    boost::optional<std::string> fullname;
    boost::optional<std::string> avatar_path;
    boost::optional<std::string> password_hash;
    boost::optional<std::string> password;
    boost::optional<std::string> ldap_dn;

    using Model = elle::das::Model<
      User,
      decltype(elle::meta::list(
                 memo::symbols::name,
                 memo::symbols::description,
                 memo::symbols::email,
                 memo::symbols::avatar_path,
                 memo::symbols::fullname,
                 memo::symbols::public_key,
                 memo::symbols::ldap_dn,
                 memo::symbols::private_key))>;
  };
}
