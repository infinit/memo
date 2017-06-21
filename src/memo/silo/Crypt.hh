#pragma once

#include <elle/cryptography/Cipher.hh>

#include <memo/silo/Silo.hh>

namespace memo
{
  namespace silo
  {
    class Crypt
      : public Silo
    {
    public:
      Crypt(std::unique_ptr<Silo> backend,
            std::string const& password,
            // Mix address and password to get a different key per block.
            bool salt = true);
      std::string
      type() const override { return "cache"; }

    protected:
      elle::Buffer
      _get(Key k) const override;
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      int
      _erase(Key k) override;
      std::vector<Key>
      _list() override;

      using SecretKey = elle::cryptography::SecretKey;
      /// The secret key corresponding to @a k.
      SecretKey
      _secret_key(Key const& k) const;

    private:
      std::unique_ptr<Silo> _backend;
      std::string _password;
      bool _salt;
    };

    struct CryptSiloConfig
      : public SiloConfig
    {
      using Super = SiloConfig;
      CryptSiloConfig(std::string name,
                         boost::optional<int64_t> capacity,
                         boost::optional<std::string> description);
      CryptSiloConfig(elle::serialization::SerializerIn& in);

      void
      serialize(elle::serialization::Serializer& s) override;

      std::unique_ptr<memo::silo::Silo>
      make() override;

      std::string password;
      bool salt;
      std::shared_ptr<SiloConfig> storage;
    };
  }
}
