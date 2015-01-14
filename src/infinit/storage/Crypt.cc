#include <infinit/storage/Crypt.hh>
#include <infinit/model/Address.hh>
#include <cryptography/SecretKey.hh>

#include <elle/factory.hh>


ELLE_LOG_COMPONENT("infinit.fs.crypt");


namespace infinit
{
  namespace storage
  {

    static
    std::unique_ptr<infinit::storage::Storage>
    make(std::vector<std::string> const& args)
    {
      std::unique_ptr<Storage> backend = instantiate(args[0], args[1]);
      std::string const& password = args[2];
      bool salt = true;
      if (args.size() > 3)
      {
        std::string const& v = args[3];
        salt = (v=="1" || v == "yes" || v == "true");
      }
      return elle::make_unique<Crypt>(std::move(backend), password, salt);
    }

    typedef  infinit::cryptography::SecretKey SecretKey;

    Crypt::Crypt(std::unique_ptr<Storage> backend,
                 std::string const& password,
                 bool salt,
                 infinit::cryptography::cipher::Algorithm algorithm)
      : _backend(std::move(backend))
      , _password(password)
      , _salt(salt)
      , _algorithm(algorithm)
    {}

    elle::Buffer
    Crypt::_get(Key k) const
    {
      elle::Buffer e = this->_backend->get(k);
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
      this->_backend->set(k, encrypted.buffer(), insert, update);
    }

    void
    Crypt::_erase(Key k)
    {
      this->_backend->erase(k);
    }
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "crypt", &infinit::storage::make);
