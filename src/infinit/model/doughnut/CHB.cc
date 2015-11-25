#include <cryptography/hash.hh>
#include <elle/bench.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class CHB
        : public elle::Buffer, public blocks::ImmutableBlock
      {
      // Types
      public:
        typedef CHB Self;
        typedef blocks::ImmutableBlock Super;


      // Construction
      public:
        CHB(elle::Buffer data)
          : elle::Buffer(_make_salt())
          , Super(CHB::_hash_address(data, *this), data)
          , _salt(*this)
        {}

        CHB(CHB const& other)
          : Super(other)
          , _salt(other._salt)
        {}

      // Clone.
        virtual
        std::unique_ptr<blocks::Block>
        clone() const override
        {
          return std::unique_ptr<blocks::Block>(new CHB(*this));
        }

      // Validation
      protected:
        virtual
        void
        _seal() override
        {}

        virtual
        blocks::ValidationResult
        _validate() const override
        {
          ELLE_DEBUG_SCOPE("%s: validate", *this);
          auto expected_address = CHB::_hash_address(this->data(), this->_salt);
          if (this->address() != expected_address)
          {
            auto reason =
              elle::sprintf("address %x invalid, expecting %x",
                            this->address(), expected_address);
            ELLE_DUMP("%s: %s", *this, reason);
            return blocks::ValidationResult::failure(reason);
          }
          return blocks::ValidationResult::success();
        }

      // Serialization
      public:
        CHB(elle::serialization::Serializer& input)
          : Super(input)
        {
          input.serialize("salt", _salt);
        }
        void serialize(elle::serialization::Serializer& s) override
        {
          Super::serialize(s);
          s.serialize("salt", _salt);
        }
      // Details
      private:
        static
        elle::Buffer
        _make_salt()
        {
          return elle::Buffer(Address::random().value(),
                                          sizeof(Address::Value));
        }

        static
        Address
        _hash_address(elle::Buffer const& content, elle::Buffer const& salt)
        {
          static elle::Bench bench("bench.chb.hash", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          elle::IOStream stream(salt.istreambuf_combine(content));
          elle::Buffer hash;
          if (content.size() > 262144)
          {
            reactor::background([&] {
              hash = cryptography::hash(stream, cryptography::Oneway::sha256);
            });
          }
          else
            hash = cryptography::hash(stream, cryptography::Oneway::sha256);
          return Address(hash.contents());
        }
        elle::Buffer _salt;
      };

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<CHB> _register_chb_serialization("CHB");
    }
  }
}
