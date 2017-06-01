#pragma once

#include <elle/cryptography/Cipher.hh>

#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Crypt
      : public Storage
    {
    public:
      Crypt(std::unique_ptr<Storage> backend,
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
      std::unique_ptr<Storage> _backend;
      std::string _password;
      bool _salt;
    };

    struct CryptStorageConfig
      : public StorageConfig
    {
      using Super = StorageConfig;
      CryptStorageConfig(std::string name,
                         boost::optional<int64_t> capacity,
                         boost::optional<std::string> description);
      CryptStorageConfig(elle::serialization::SerializerIn& in);

      void
      serialize(elle::serialization::Serializer& s) override;

      std::unique_ptr<infinit::storage::Storage>
      make() override;

      std::string password;
      bool salt;
      std::shared_ptr<StorageConfig> storage;
    };
  }
}
