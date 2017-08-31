#pragma once

#include <memo/model/blocks/Block.hh>

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      class MutableBlock
        : public Block
        , private InstanceTracker<MutableBlock>
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = MutableBlock;
        using Super = Block;
        static char const* type;


      /*-------------.
      | Construction |
      `-------------*/
        MutableBlock(MutableBlock const& other) = default;
      protected:
        MutableBlock(Address address,
                     elle::Buffer data = {},
                     Address owner = Address::null);
        friend class memo::model::Model;
        bool _data_changed;

      /*-------.
      | Clone  |
      `-------*/
      public:
        std::unique_ptr<Block>
        clone() const override;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        MutableBlock(elle::serialization::Serializer& input,
                     elle::Version const& version);

        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
      private:
        void
        _serialize(elle::serialization::Serializer& s);

      /*--------.
      | Content |
      `--------*/
      public:
        using Super::data;
        virtual
        int
        version() const /* = 0 */ { return 0; }; // FIXME
        virtual
        void
        data(elle::Buffer data);
        virtual
        void
        data(std::function<void (elle::Buffer&)> transformation);
        ELLE_ATTRIBUTE_RW(boost::optional<int>, seal_version, protected);
      };
    }
  }
}
