#include <infinit/storage/Crypt.hh>
#include <infinit/model/Address.hh>
#include <elle/cryptography/SecretKey.hh>

#include <elle/factory.hh>

namespace infinit
{
  namespace storage
  {
    namespace
    {
      std::unique_ptr<infinit::storage::Storage>
      make(std::vector<std::string> const& args)
      {
        auto backend = instantiate(args[0], args[1]);
        auto const& password = args[2];
        auto const salt = 3 < args.size() : to_bool(args[3]) : false;
        return std::make_unique<Crypt>(std::move(backend), password, salt);
      }
    }

    using SecretKey = elle::cryptography::SecretKey;

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

    int
    Crypt::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      SecretKey enc(
        _salt ? _password + elle::sprintf("%x", k) : this->_password);
      this->_backend->set(k, enc.encipher(value), insert, update);

      return 0;
    }

    int
    Crypt::_erase(Key k)
    {
      this->_backend->erase(k);
      return 0;
    }

    std::vector<Key>
    Crypt::_list()
    {
      return this->_backend->list();
    }

    CryptStorageConfig::CryptStorageConfig(
      std::string name,
      boost::optional<int64_t> capacity,
      boost::optional<std::string> description)
      : Super(name, std::move(capacity), std::move(description))
    {}

    CryptStorageConfig::CryptStorageConfig(elle::serialization::SerializerIn& s)
      : StorageConfig(s)
      , password(s.deserialize<std::string>("password"))
      , salt(s.deserialize<bool>("salt"))
      , storage(s.deserialize<std::shared_ptr<StorageConfig>>("backend"))
    {}

    void
    CryptStorageConfig::serialize(elle::serialization::Serializer& s)
    {
      StorageConfig::serialize(s);
      s.serialize("password", this->password);
      s.serialize("salt", this->salt);
      s.serialize("backend", this->storage);
    }

    std::unique_ptr<infinit::storage::Storage>
    CryptStorageConfig::make()
    {
      return std::make_unique<infinit::storage::Crypt>(
        storage->make(), password, salt);
    }
  }
}

namespace
{
  auto const reg
  = elle::serialization::Hierarchy<infinit::storage::StorageConfig>::
      Register<infinit::storage::CryptStorageConfig>("crypt");

  FACTORY_REGISTER(infinit::storage::Storage, "crypt", &infinit::storage::make);
}
