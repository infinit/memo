#ifndef INFINIT_STORAGE_CRYPT_HH
#define INFINIT_STORAGE_CRYPT_HH

#include <cryptography/Cipher.hh>

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
            bool salt = true
        );
    protected:
      virtual
      elle::Buffer
      _get(Key k) const override;
      virtual
      int
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      int
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;
    private:
      std::unique_ptr<Storage> _backend;
      std::string _password;
      bool _salt;
    };

    struct CryptStorageConfig
      : public StorageConfig
    {
      CryptStorageConfig(std::string name, int capacity = 0);
      CryptStorageConfig(elle::serialization::SerializerIn& in);

      void
      serialize(elle::serialization::Serializer& s) override;

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override;

      std::string password;
      bool salt;
      std::shared_ptr<StorageConfig> storage;
    };
  }
}

#endif
