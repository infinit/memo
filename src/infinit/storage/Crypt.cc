#include <infinit/storage/Crypt.hh>
#include <infinit/model/Address.hh>
#include <cryptography/SecretKey.hh>

ELLE_LOG_COMPONENT("infinit.fs.crypt");
namespace infinit
{
  namespace storage
  {
    typedef  infinit::cryptography::SecretKey SecretKey;

    Crypt::Crypt(Storage& backend, std::string const& password,
            bool salt, // mix address and password to get a different key per block
            infinit::cryptography::cipher::Algorithm algorithm)
      : _backend(backend)
      , _password(password)
      , _salt(salt)
      , _algorithm(algorithm)
    {}
    elle::Buffer
    Crypt::_get(Key k) const
    {
      elle::Buffer e = _backend.get(k);
      SecretKey enc(_algorithm,
        _salt ? _password + elle::sprintf("%x", k) : _password);
      auto out = enc.decrypt<elle::Buffer>(infinit::cryptography::Output(e));
      return std::move(out);
    }
    void
    Crypt::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      SecretKey enc(_algorithm,
        _salt ? _password + elle::sprintf("%x", k) : _password);
      auto encrypted = enc.encrypt(value);
      _backend.set(k, encrypted.buffer(), insert, update);
    }
    void
    Crypt::_erase(Key k)
    {
      _backend.erase(k);
    }
  }
}