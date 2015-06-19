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
          : Super(CHB::_hash_address(data), data)
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
          auto expected_address = CHB::_hash_address(this->data());
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
        {}

      // Details
      private:
        static
        Address
        _hash_address(elle::Buffer const& content)
        {
          auto hash = cryptography::oneway::hash
            (cryptography::Plain(content),
             cryptography::oneway::Algorithm::sha256);
          return Address(hash.buffer().contents());
        }
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<CHB> _register_chb_serialization("CHB");
    }
  }
}
