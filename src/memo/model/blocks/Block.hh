#pragma once

#include <elle/Buffer.hh>
#include <elle/Printable.hh>
#include <elle/Clonable.hh>

#include <elle/cryptography/rsa/PublicKey.hh>

#include <memo/model/Address.hh>
#include <memo/model/blocks/ValidationResult.hh>
#include <memo/model/fwd.hh>
#include <memo/model/prometheus.hh>
#include <memo/serialization.hh>

namespace memo
{
  namespace model
  {
    namespace blocks
    {
      struct RemoveSignature
      {
        using serialization_tag = memo::serialization_tag;
        RemoveSignature();
        RemoveSignature(RemoveSignature const& other);
        RemoveSignature(RemoveSignature && other) = default;
        RemoveSignature(elle::serialization::Serializer& input);
        RemoveSignature& operator = (RemoveSignature && other) = default;
        void serialize(elle::serialization::Serializer& s);
        std::unique_ptr<Block> block;
        boost::optional<elle::cryptography::rsa::PublicKey> group_key;
        boost::optional<int> group_index;
        boost::optional<elle::cryptography::rsa::PublicKey> signature_key;
        boost::optional<elle::Buffer> signature;
      };

#if MEMO_ENABLE_PROMETHEUS
      /// A gauge family to track the number of blocks.
      ///
      /// May return nullptr if set up failed.
      memo::prometheus::Family<memo::prometheus::Gauge>*
      block_gauge_family();

      /// A base class to bind a gauge to a number of instances.
      template <typename Self>
      struct InstanceTracker
      {
        InstanceTracker()
        {
          _block_gauge_increment();
        }

        InstanceTracker(InstanceTracker&&)
        {
          _block_gauge_increment();
        }

        InstanceTracker(InstanceTracker const&)
        {
          _block_gauge_increment();
        }

        ~InstanceTracker()
        {
          _block_gauge_decrement();
        }

        static
        memo::prometheus::Gauge*
        _block_gauge()
        {
          static auto res = memo::prometheus::instance()
            .make(block_gauge_family(),
                  {{"type", Self::type}});
          return res.get();
        }

        static
        void
        _block_gauge_increment()
        {
          if (auto g = _block_gauge())
          {
            ELLE_LOG_COMPONENT("memo.model.blocks.Block.prometheus");
            ELLE_DUMP("increment: %s", Self::type);
            g->Increment();
          }
        }

        static
        void
        _block_gauge_decrement()
        {
          if (auto g = _block_gauge())
          {
            ELLE_LOG_COMPONENT("memo.model.blocks.Block.prometheus");
            ELLE_DUMP("decrement: %s", Self::type);
            g->Decrement();
          }
        }
      };
#else
      template <typename Self>
      struct InstanceTracker {};
#endif

      class Block
        : public elle::Printable
        , public elle::serialization::VirtuallySerializable<Block, true>
        , public elle::Clonable<Block>
        , private InstanceTracker<Block>
      {
      /*-------------.
      | Construction |
      `-------------*/
      public:
        Block(Address address, elle::Buffer data = {});
        Block(Block const& other) = default;
        Block(Block&& other) = default;
        friend class memo::model::Model;
        static char const* type;

      /*---------.
      | Clonable |
      `---------*/
      public:
        std::unique_ptr<Block>
        clone() const override;

      /*--------.
      | Content |
      `--------*/
      public:
        virtual
        bool
        operator ==(Block const& rhs) const;
        elle::Buffer
        take_data();
        ELLE_ATTRIBUTE_R(Address, address, protected);
        ELLE_ATTRIBUTE_R(elle::Buffer, data, protected, virtual);

      /*-----------.
      | Validation |
      `-----------*/
      public:
        void
        seal(boost::optional<int> version = {});
        ValidationResult
        validate(Model const& model, bool writing) const;
        ValidationResult
        validate(Model const& model, const Block& new_block) const;
        void
        stored(); // called right after a successful store
        /// Generate signature for removal request.
        RemoveSignature
        sign_remove(Model& model) const;
        ValidationResult
        validate_remove(Model& model, RemoveSignature const& sig) const;
        ELLE_ATTRIBUTE_RW(bool, validated, protected);
        /// Put the block in decrypted state. It might not be possible to seal it again.
        void
        decrypt();
      protected:
        virtual
        void
        _seal(boost::optional<int> version);
        virtual
        ValidationResult
        _validate(Model const& model, bool writing) const;
        virtual
        ValidationResult
        _validate(Model const& model, const Block& new_block) const;
        virtual
        void
        _stored();
        virtual
        RemoveSignature
        _sign_remove(Model& model) const;
        virtual
        ValidationResult
        _validate_remove(Model& model,
                         RemoveSignature const& sig) const;
        virtual
        void
        _decrypt();

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        static constexpr char const* virtually_serializable_key = "type";
        Block(elle::serialization::Serializer& input,
              elle::Version const& version);
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
        using serialization_tag = memo::serialization_tag;

      /*----------.
      | Printable |
      `----------*/
      public:
        void
        print(std::ostream& output) const override;
      };
    }
  }
}
