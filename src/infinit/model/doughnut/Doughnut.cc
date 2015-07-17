#include <infinit/model/doughnut/Doughnut.hh>

#include <elle/Buffer.hh>
#include <elle/Error.hh>
#include <elle/IOStream.hh>
#include <elle/cast.hh>
#include <elle/log.hh>
#include <elle/serialization/json.hh> // FIXME

#include <reactor/Scope.hh>
#include <reactor/exception.hh>

#include <infinit/storage/MissingKey.hh>

#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <infinit/model/doughnut/OKB.hh>
#include <infinit/model/doughnut/Remote.hh>
#include <infinit/model/doughnut/User.hh>

ELLE_LOG_COMPONENT("infinit.model.doughnut.Doughnut");

#include <infinit/model/doughnut/CHB.cc>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      class PlainMutableBlock: public blocks::MutableBlock
      {
      public:
        typedef blocks::MutableBlock Super;
        PlainMutableBlock()
        : Super(Address::random()) {}
        PlainMutableBlock(elle::serialization::Serializer& input)
        : Super(input) {}
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<PlainMutableBlock> _register_pmb_serialization("PMB");
      class PlainACLBlock: public blocks::ACLBlock
      {
      public:
        typedef blocks::ACLBlock Super;
        PlainACLBlock()
        : Super(Address::random()) {}
        PlainACLBlock(elle::serialization::Serializer& input)
        : Super(input) {}
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<PlainACLBlock> _register_paclb_serialization("PACLB");
      class PlainImmutableBlock: public blocks::ImmutableBlock
      {
      public:
        PlainImmutableBlock()
        : Super(Address::random()) {}
        PlainImmutableBlock(elle::Buffer b)
        : Super(Address::random(), b)
        {}
        PlainImmutableBlock(elle::serialization::Serializer& input)
        : Super(input)
        {}
        typedef blocks::ImmutableBlock Super;
      };
      static const elle::serialization::Hierarchy<blocks::Block>::
      Register<PlainImmutableBlock> _register_pib_serialization("PIB");

      Doughnut::Doughnut(cryptography::rsa::KeyPair keys,
                         std::unique_ptr<overlay::Overlay> overlay,
                         std::unique_ptr<Consensus> consensus,
                         bool plain)
        : _overlay(std::move(overlay))
        , _consensus(std::move(consensus))
        , _keys(std::move(keys))
        , _plain(plain)
      {
        if (!this->_consensus)
          this->_consensus = elle::make_unique<Consensus>(*this);
      }

      std::unique_ptr<blocks::MutableBlock>
      Doughnut::_make_mutable_block() const
      {
        ELLE_TRACE_SCOPE("%s: create OKB", *this);
        if (_plain)
           return elle::make_unique<PlainMutableBlock>();
        auto res = elle::make_unique<OKB>(const_cast<Doughnut*>(this));
        return std::move(res);
      }

      std::unique_ptr<blocks::ImmutableBlock>
      Doughnut::_make_immutable_block(elle::Buffer content) const
      {
        ELLE_TRACE_SCOPE("%s: create CHB", *this);
        if (_plain)
          return elle::make_unique<PlainImmutableBlock>(std::move(content));
        else
          return elle::make_unique<CHB>(std::move(content));
      }

      std::unique_ptr<blocks::ACLBlock>
      Doughnut::_make_acl_block() const
      {
        ELLE_TRACE_SCOPE("%s: create ACB", *this);
        if (_plain)
          return elle::make_unique<PlainACLBlock>();
        else
          return elle::make_unique<ACB>(const_cast<Doughnut*>(this));
      }

      std::unique_ptr<model::User>
      Doughnut::_make_user(elle::Buffer const& data) const
      {
        elle::IOStream input(data.istreambuf());
        elle::serialization::json::SerializerIn s(input);
        return elle::make_unique<doughnut::User>(cryptography::rsa::PublicKey(s));
      }

      void
      Doughnut::_store(blocks::Block& block, StoreMode mode)
      {
        this->_consensus->store(*this->_overlay, block, mode);
      }

      std::unique_ptr<blocks::Block>
      Doughnut::_fetch(Address address) const
      {
        std::unique_ptr<blocks::Block> res;
        try
        {
          return this->_consensus->fetch(*this->_overlay, address);
        }
        catch (infinit::storage::MissingKey const&)
        {
          return nullptr;
        }
      }

      void
      Doughnut::_remove(Address address)
      {
        this->_consensus->remove(*this->_overlay, address);
      }

      struct DoughnutModelConfig:
        public ModelConfig
      {
      public:
        std::unique_ptr<infinit::overlay::OverlayConfig> overlay;
        std::unique_ptr<infinit::cryptography::rsa::KeyPair> key;
        boost::optional<bool> plain;;

        DoughnutModelConfig(elle::serialization::SerializerIn& input)
          : ModelConfig()
        {
          this->serialize(input);
        }

        void
        serialize(elle::serialization::Serializer& s)
        {
          s.serialize("overlay", this->overlay);
          s.serialize("keys", this->key);
          s.serialize("plain", this->plain);
        }

        virtual
        std::unique_ptr<infinit::model::Model>
        make()
        {
          if (!key)
            return elle::make_unique<infinit::model::doughnut::Doughnut>(
              infinit::cryptography::rsa::keypair::generate(2048),
              overlay->make(),
              nullptr,
              plain && *plain);
          else
            return elle::make_unique<infinit::model::doughnut::Doughnut>(
              std::move(*key),
              overlay->make(),
              nullptr,
              plain && *plain);
        }
      };

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<DoughnutModelConfig> _register_DoughnutModelConfig("doughnut");
    }
  }
}
