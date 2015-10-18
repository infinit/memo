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
        MutableBlock(MutableBlock const& other);
      protected:
        MutableBlock(Address address);
        MutableBlock(Address address, elle::Buffer data);
        friend class infinit::model::Model;
        bool _data_changed;

      /*-------.
      | Clone  |
      `-------*/
      public:
        virtual
        std::unique_ptr<Block>
        clone() const override;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        MutableBlock(elle::serialization::Serializer& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
      private:
        void
        _serialize(elle::serialization::Serializer& s);

      /*--------.
      | Content |
      `--------*/
      public:
        using Super::data;
        virtual
        void
        data(elle::Buffer data);
        virtual
        void
        data(std::function<void (elle::Buffer&)> transformation);

        class Cache
        {
        public:
          virtual ~Cache() {}
        };
        /** Use this function to speedup block unsealing if the block
         *  has not changed.
         *  Call cache_update() before the first call to data().
        */
        virtual
        std::unique_ptr<Cache>
        cache_update(std::unique_ptr<Cache> previous);
        ELLE_ATTRIBUTE_RP(bool, is_local, protected:)
      };
    }
  }
}

#endif
