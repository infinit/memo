#ifndef INFINIT_MODEL_BLOCKS_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_BLOCK_HH

# include <elle/Buffer.hh>
# include <elle/Printable.hh>

# include <infinit/model/Address.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class Block
        : public elle::Printable
      {
      /*-------------.
      | Construction |
      `-------------*/

      public:
        Block(Address address);
        Block(Address address, elle::Buffer data);
        ELLE_ATTRIBUTE_R(Address, address);
        ELLE_ATTRIBUTE_RX(elle::Buffer, data);
        bool
        operator ==(Block const& rhs) const;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        void
        serialize(elle::serialization::Serializer& s);

      /*----------.
      | Printable |
      `----------*/

      public:
        virtual
        void
        print(std::ostream& output) const override;
      };
    }
  }
}

#endif
