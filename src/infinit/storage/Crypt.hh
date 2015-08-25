#ifndef INFINIT_STORAGE_CRYPT_HH
#define INFINIT_STORAGE_CRYPT_HH

#include <cryptography/Cipher.hh>

#include <infinit/storage/Storage.hh>

namespace infinit
{
  namespace storage
  {
    class Crypt: public Storage
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
      void
      _set(Key k, elle::Buffer const& value, bool insert, bool update) override;
      virtual
      void
      _erase(Key k) override;
      virtual
      std::vector<Key>
      _list() override;
    private:
      std::unique_ptr<Storage> _backend;
      std::string _password;
      bool _salt;
    };
  }
}

#endif
