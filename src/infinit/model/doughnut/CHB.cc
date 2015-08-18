#include <cryptography/hash.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class CHB
        : public blocks::ImmutableBlock
      {
      // Types
      public:
        typedef CHB Self;
        typedef blocks::ImmutableBlock Super;


      // Construction
      public:
        CHB(elle::Buffer data)
          : Super(CHB::_hash_address(data, _make_salt()), data)
          , _salt(_last_salt())
        {}

      // Validation
      protected:
        virtual
        void
        _seal() override
        {}

        virtual
        bool
        _validate() const override
        {
          ELLE_DEBUG_SCOPE("%s: validate", *this);
          auto expected_address = CHB::_hash_address(this->data(), this->_salt);
          if (this->address() != expected_address)
          {
            ELLE_DUMP("%s: address %x invalid, expecting %x",
                      *this, this->address(), expected_address);
            return false;
          }
          return true;
        }

      // Serialization
      public:
        CHB(elle::serialization::Serializer& input)
          : Super(input)
        {
          input.serialize("salt", _salt);
        }
        void serialize(elle::serialization::Serializer& s)
        {
          Super::serialize(s);
          s.serialize("salt", _salt);
        }
      // Details
      private:
        static elle::Buffer _last_salt_value;
        static
        elle::Buffer
        _make_salt()
        {
          _last_salt_value = elle::Buffer(Address::random().value(),
                                          sizeof(Address::Value));
          return _last_salt_value;
        }

        static
        elle::Buffer
        _last_salt()
        {
          return _last_salt_value;
        }

        static
        Address
        _hash_address(elle::Buffer const& content, elle::Buffer const& salt)
        {
          elle::IOStream stream(salt.istreambuf_combine(content));
          auto hash = cryptography::hash
            (stream, cryptography::Oneway::sha256);
          return Address(hash.contents());
        }
        elle::Buffer _salt;
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<CHB> _register_chb_serialization("CHB");

      elle::Buffer CHB::_last_salt_value;
    }
  }
}
