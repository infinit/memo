#ifndef INFINIT_MODEL_BLOCKS_MUTABLE_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_MUTABLE_BLOCK_HH

# include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class MutableBlock
        : public Block
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef MutableBlock Self;
        typedef Block Super;

      /*-------------.
      | Construction |
      `-------------*/
      protected:
        MutableBlock(Address address);
        MutableBlock(Address address, elle::Buffer data);
        friend class infinit::model::Model;

      /*--------.
      | Content |
      `--------*/
      public:
        elle::Buffer&
        data();
      };
    }
  }
}

#endif
