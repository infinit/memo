#ifndef INFINIT_STORAGE_CRYPT_HH
#define INFINIT_STORAGE_CRYPT_HH

#include <infinit/storage/Storage.hh>
#include <cryptography/Cipher.hh>

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
            bool salt = true,
            infinit::cryptography::Cipher algorithm
              = infinit::cryptography::Cipher::aes256
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
    private:
      std::unique_ptr<Storage> _backend;
      std::string _password;
      bool _salt;
      infinit::cryptography::Cipher _algorithm;
    };
  }
}

#endif
