#ifndef INFINIT_STORAGE_CRYPT_HH
#define INFINIT_STORAGE_CRYPT_HH

#include <infinit/storage/Storage.hh>
#include <cryptography/cipher.hh>

namespace infinit
{
  namespace storage
  {
    class Crypt: public Storage
    {
    public:
      Crypt(Storage& backend, std::string const& password,
            bool salt = true, // mix address and password to get a different key per block
            infinit::cryptography::cipher::Algorithm algorithm
              = infinit::cryptography::cipher::Algorithm::aes256
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
      Storage& _backend;
      std::string _password;
      bool _salt;
      infinit::cryptography::cipher::Algorithm _algorithm;
    };
  }
}

#endif