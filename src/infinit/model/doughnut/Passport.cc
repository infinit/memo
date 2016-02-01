#include <elle/serialization/json.hh>
#include <elle/utils.hh>

#include <infinit/model/doughnut/Passport.hh>
#include <infinit/model/doughnut/Doughnut.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Passport");

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      Passport::Passport(cryptography::rsa::PublicKey user,
                         std::string network,
                         cryptography::rsa::KeyPair const& owner,
                         bool store_certifier,
                         bool allow_write,
                         bool allow_storage,
                         bool allow_sign)
        : _user(std::move(user))
        , _network(std::move(network))
        , _signature()
        , _allow_write(allow_write)
        , _allow_storage(allow_storage)
        , _allow_sign(allow_sign)
      {
        elle::Buffer fingerprint;
        {
          elle::IOStream output(fingerprint.ostreambuf());
          elle::serialization::json::SerializerOut s(output, false);
          s.serialize("user", this->_user);
          s.serialize("network", this->_network);
          s.serialize("allow_write", this->_allow_write);
          s.serialize("allow_storage", this->_allow_storage);
          s.serialize("allow_sign", this->_allow_sign);
        }
        this->_signature = owner.k().sign(fingerprint);
        if (store_certifier)
          this->_certifier.emplace(owner.K());
      }

      Passport::Passport(elle::serialization::SerializerIn& s)
        : _user(s.deserialize<cryptography::rsa::PublicKey>("user"))
        , _network(s.deserialize<std::string>("network"))
        , _signature(s.deserialize<elle::Buffer>("signature"))
        , _allow_write(true)
        , _allow_storage(true)
        , _allow_sign(false)
      {
        Doughnut* dn;
        elle::unconst(s.context()).get<Doughnut*>(dn, nullptr);
        if (!dn || dn->version() >= elle::Version(0, 5, 0))
          try
          { // passports are transmited unversioned, no way to check except trying
            _allow_write = s.deserialize<bool>("allow_write");
            _allow_storage = s.deserialize<bool>("allow_storage");
            _allow_sign = s.deserialize<bool>("allow_sign");
            _certifier = s.deserialize
              <boost::optional<cryptography::rsa::PublicKey>>("certifier");
          }
          catch (elle::Error const& e)
          {
            ELLE_TRACE(
              "Passport deserialization error, assuming older version");
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
        Doughnut* dn;
        elle::unconst(s.context()).get<Doughnut*>(dn, nullptr);
        if (!dn || dn->version() >= elle::Version(0, 5, 0))
        {
          ELLE_DEBUG("serialize in 5: %s", dn);
          //ELLE_DEBUG("%s", elle::Backtrace::current());
          s.serialize("allow_write", this->_allow_write);
          s.serialize("allow_storage", this->_allow_storage);
          s.serialize("allow_sign", this->_allow_sign);
          s.serialize("certifier", this->_certifier);
        }
        else
          ELLE_DEBUG("serialize in 4");
      }

      bool
      Passport::verify(cryptography::rsa::PublicKey const& owner) const
      {
        if (this->_certifier && *this->_certifier != owner)
        {
          ELLE_TRACE("owner key does not match certifier");
          return false;
        }
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
              s.serialize("allow_sign", this->_allow_sign);
            }
          }
          if (owner.verify(this->_signature, fingerprint))
          {
            if (i == 1 && this->_allow_sign)
            { // nice try
              ELLE_TRACE("Legacy signature validates but allow_sign is set");
              return false;
            }
            return true;
          }
        }
        return false;
      }
    }
  }
}
