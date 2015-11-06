#include <infinit/storage/Crypt.hh>
#include <infinit/model/Address.hh>
#include <cryptography/SecretKey.hh>

#include <elle/factory.hh>

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
        salt = (v == "1" || v == "yes" || v == "true");
      }
      return elle::make_unique<Crypt>(std::move(backend), password, salt);
    }

    typedef  infinit::cryptography::SecretKey SecretKey;

    Crypt::Crypt(std::unique_ptr<Storage> backend,
                 std::string const& password,
                 bool salt)
      : _backend(std::move(backend))
      , _password(password)
      , _salt(salt)
    {}

    elle::Buffer
    Crypt::_get(Key k) const
    {
      elle::Buffer e = this->_backend->get(k);
      SecretKey enc(_salt ? _password + elle::sprintf("%x", k) : _password);
      return enc.decipher(e);
    }

    void
    Crypt::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      SecretKey enc(
        _salt ? _password + elle::sprintf("%x", k) : this->_password);
      this->_backend->set(k, enc.encipher(value), insert, update);
    }

    void
    Crypt::_erase(Key k)
    {
      this->_backend->erase(k);
    }

    std::vector<Key>
    Crypt::_list()
    {
      return this->_backend->list();
    }

    struct CryptStorageConfig:
    public StorageConfig
    {
    public:
      std::string password;
      bool salt;
      std::shared_ptr<StorageConfig> storage;
      CryptStorageConfig(elle::serialization::SerializerIn& input)
      : StorageConfig()
      {
        this->serialize(input);
      }

      void
      serialize(elle::serialization::Serializer& s) override
      {
        StorageConfig::serialize(s);
        s.serialize("password", this->password);
        s.serialize("salt", this->salt);
        s.serialize("backend", this->storage);
      }

      virtual
      std::unique_ptr<infinit::storage::Storage>
      make() override
      {
        return elle::make_unique<infinit::storage::Crypt>(
          std::move(storage->make()), password, salt);
      }
    };

    static const elle::serialization::Hierarchy<StorageConfig>::
    Register<CryptStorageConfig>
    _register_CryptStorageConfig("crypt");
  }
}

FACTORY_REGISTER(infinit::storage::Storage, "crypt", &infinit::storage::make);
