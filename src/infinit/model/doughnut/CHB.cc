#include <elle/bench.hh>
#include <elle/log.hh>

#include <cryptography/hash.hh>

#include <reactor/duration.hh>
#include <reactor/scheduler.hh>

#include <infinit/model/doughnut/CHB.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.CHB")

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      /*-------------.
      | Construction |
      `-------------*/

      CHB::CHB(elle::Buffer data)
        : CHB(std::move(data), this->_make_salt())
      {}

      CHB::CHB(elle::Buffer data, elle::Buffer salt)
        : Super(CHB::_hash_address(data, salt), data)
        , _salt(std::move(salt))
      {}

      CHB::CHB(CHB const& other)
        : Super(other)
        , _salt(other._salt)
      {}

      /*---------.
      | Clonable |
      `---------*/

      std::unique_ptr<blocks::Block>
      CHB::clone(bool) const
      {
        return std::unique_ptr<blocks::Block>(new CHB(*this));
      }

      /*-----------.
      | Validation |
      `-----------*/

      void
      CHB::_seal()
      {}

      blocks::ValidationResult
      CHB::_validate() const
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

      /*--------------.
      | Serialization |
      `--------------*/

      CHB::CHB(elle::serialization::Serializer& input)
        : Super(input)
      {
        input.serialize("salt", _salt);
      }

      void
      CHB::serialize(elle::serialization::Serializer& s)
      {
        Super::serialize(s);
        s.serialize("salt", _salt);
      }

      /*--------.
      | Details |
      `--------*/

      elle::Buffer
      CHB::_make_salt()
      {
        return elle::Buffer(Address::random().value(),
                            sizeof(Address::Value));
      }

      Address
      CHB::_hash_address(elle::Buffer const& content, elle::Buffer const& salt)
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

      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<CHB> _register_chb_serialization("CHB");
    }
  }
}
