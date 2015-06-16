#ifndef INFINIT_MODEL_BLOCKS_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_BLOCK_HH

# include <elle/Buffer.hh>
# include <elle/Printable.hh>

# include <infinit/model/Address.hh>
# include <infinit/model/fwd.hh>

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
        ELLE_ATTRIBUTE_R(Address, address);
        ELLE_ATTRIBUTE_RX(elle::Buffer, data);
        bool
        operator ==(Block const& rhs) const;
      protected:
        Block(Address address);
        Block(Address address, elle::Buffer data);
        friend class infinit::model::Model;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        Block(elle::serialization::Serializer& input);
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
