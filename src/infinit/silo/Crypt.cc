#include <infinit/silo/Crypt.hh>
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
        auto const salt = 3 < args.size() ? to_bool(args[3]) : false;
        return std::make_unique<Crypt>(std::move(backend), password, salt);
      }
    }

    Crypt::Crypt(std::unique_ptr<Storage> backend,
                 std::string const& password,
                 bool salt)
      : _backend(std::move(backend))
      , _password(password)
      , _salt(salt)
    {}

    auto
    Crypt::_secret_key(Key const& k) const
      -> SecretKey
    {
      return _salt ? _password + elle::sprintf("%x", k) : _password;
    }

    elle::Buffer
    Crypt::_get(Key k) const
    {
      auto const enc = this->_backend->get(k);
      return _secret_key(k).decipher(enc);
    }

    int
    Crypt::_set(Key k, elle::Buffer const& value, bool insert, bool update)
    {
      return this->_backend->set(k, _secret_key(k).encipher(value), insert, update);
    }

    int
    Crypt::_erase(Key k)
    {
      return this->_backend->erase(k);
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
