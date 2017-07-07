#include <memo/silo/Crypt.hh>

#include <elle/cryptography/SecretKey.hh>
#include <elle/factory.hh>
#include <elle/from-string.hh>

#include <memo/model/Address.hh>

namespace memo
{
  namespace silo
  {
    namespace
    {
      std::unique_ptr<memo::silo::Silo>
      make(std::vector<std::string> const& args)
      {
        auto backend = instantiate(args[0], args[1]);
        auto const& password = args[2];
        auto const salt = 3 < args.size() ? elle::from_string<bool>(args[3]) : false;
        return std::make_unique<Crypt>(std::move(backend), password, salt);
      }
    }

    Crypt::Crypt(std::unique_ptr<Silo> backend,
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

    CryptSiloConfig::CryptSiloConfig(
      std::string name,
      boost::optional<int64_t> capacity,
      boost::optional<std::string> description)
      : Super(name, std::move(capacity), std::move(description))
    {}

    CryptSiloConfig::CryptSiloConfig(elle::serialization::SerializerIn& s)
      : SiloConfig(s)
      , password(s.deserialize<std::string>("password"))
      , salt(s.deserialize<bool>("salt"))
      , storage(s.deserialize<std::shared_ptr<SiloConfig>>("backend"))
    {}

    void
    CryptSiloConfig::serialize(elle::serialization::Serializer& s)
    {
      SiloConfig::serialize(s);
      s.serialize("password", this->password);
      s.serialize("salt", this->salt);
      s.serialize("backend", this->storage);
    }

    std::unique_ptr<memo::silo::Silo>
    CryptSiloConfig::make()
    {
      return std::make_unique<memo::silo::Crypt>(
        storage->make(), password, salt);
    }
  }
}

namespace
{
  auto const reg
  = elle::serialization::Hierarchy<memo::silo::SiloConfig>::
      Register<memo::silo::CryptSiloConfig>("crypt");

  FACTORY_REGISTER(memo::silo::Silo, "crypt", &memo::silo::make);
}
