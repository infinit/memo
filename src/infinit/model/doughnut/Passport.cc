#include <elle/serialization/json.hh>

#include <infinit/model/doughnut/Passport.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Passport::Passport(cryptography::rsa::PublicKey user,
                         std::string network,
                         cryptography::rsa::PrivateKey const& owner,
                         bool allow_write,
                         bool allow_storage)
        : _user(std::move(user))
        , _network(std::move(network))
        , _signature()
        , _allow_write(allow_write)
        , _allow_storage(allow_storage)
      {
        elle::Buffer fingerprint;
        {
          elle::IOStream output(fingerprint.ostreambuf());
          elle::serialization::json::SerializerOut s(output, false);
          s.serialize("user", this->_user);
          s.serialize("network", this->_network);
          s.serialize("allow_write", this->_allow_write);
          s.serialize("allow_storage", this->_allow_storage);
        }
        this->_signature = owner.sign(fingerprint);
      }

      Passport::Passport(elle::serialization::SerializerIn& s)
        : _user(s.deserialize<cryptography::rsa::PublicKey>("user"))
        , _network(s.deserialize<std::string>("network"))
        , _signature(s.deserialize<elle::Buffer>("signature"))
        , _allow_write(true)
        , _allow_storage(true)
      {
        try
        { // passports are transmited unversioned, no way to check except trying
          _allow_write = s.deserialize<bool>("allow_write");
          _allow_storage = s.deserialize<bool>("allow_storage");
        }
        catch (elle::Error const& e)
        {
        }
      }

      Passport::~Passport()
      {}

      void
      Passport::serialize(elle::serialization::Serializer& s)
      {
        ELLE_ASSERT(s.out());
        s.serialize("user", this->_user);
        s.serialize("network", this->_network);
        s.serialize("signature", this->_signature);
        s.serialize("allow_write", this->_allow_write);
        s.serialize("allow_storage", this->_allow_storage);
      }

      bool
      Passport::verify(cryptography::rsa::PublicKey const& owner)
      {
        for (int i=0; i<2; ++i)
        {
          // Attempt with and without acl arguments.
          elle::Buffer fingerprint;
          {
            elle::IOStream output(fingerprint.ostreambuf());
            elle::serialization::json::SerializerOut s(output, false);
            s.serialize("user", this->_user);
            s.serialize("network", this->_network);
            if (i == 0)
            {
              s.serialize("allow_write", this->_allow_write);
              s.serialize("allow_storage", this->_allow_storage);
            }
          }
          if (owner.verify(this->_signature, fingerprint))
            return true;
        }
        return false;
      }
    }
  }
}
