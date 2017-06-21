#include <memo/model/faith/Faith.hh>

#include <boost/uuid/random_generator.hpp>

#include <elle/log.hh>
#include <elle/serialization/binary.hh>

#include <elle/cryptography/hash.hh>

#include <memo/model/Address.hh>
#include <memo/model/MissingBlock.hh>
#include <memo/model/blocks/MutableBlock.hh>
#include <memo/silo/MissingKey.hh>

ELLE_LOG_COMPONENT("memo.model.faith.Faith");

using Serializer = elle::serialization::Binary;

namespace memo
{
  namespace model
  {
    namespace faith
    {
      Faith::Faith(std::unique_ptr<silo::Silo> storage,
                   boost::optional<elle::Version> version)
        : Model(std::move(version))
        , _storage(std::move(storage))
      {}

      std::unique_ptr<blocks::Block>
      Faith::_fetch(Address address, boost::optional<int>) const
      {
        ELLE_TRACE_SCOPE("%s: fetch block at %x", *this, address);
        try
        {
          auto data = this->_storage->get(address);
          return elle::serialization::deserialize<
            Serializer, std::unique_ptr<blocks::Block>>(data, false);
        }
        catch (memo::silo::MissingKey const&)
        {
          throw MissingBlock(address);
        }
      }

      void
      Faith::_insert(std::unique_ptr<blocks::Block> block,
                     std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: insert %f", *this, *block);
        this->_storage->set(
          block->address(),
          elle::serialization::serialize<Serializer>(block, false),
          true,
          false);
      }

      void
      Faith::_update(std::unique_ptr<blocks::Block> block,
                     std::unique_ptr<ConflictResolver> resolver)
      {
        ELLE_TRACE_SCOPE("%s: update %f", *this, *block);
        this->_storage->set(
          block->address(),
          elle::serialization::serialize<Serializer>(block, false),
          false,
          true);
      }

      void
      Faith::_remove(Address address, blocks::RemoveSignature)
      {
        ELLE_TRACE_SCOPE("%s: remove block at %x", *this, address);
        try
        {
          this->_storage->erase(address);
        }
        catch (memo::silo::MissingKey const&)
        {
          throw MissingBlock(address);
        }
      }

      struct FaithModelConfig:
        public ModelConfig
      {
      public:
        FaithModelConfig(elle::serialization::SerializerIn& input,
                         elle::Version version)
          : ModelConfig(nullptr, std::move(version))
        {
          this->serialize(input);
        }

        void
        serialize(elle::serialization::Serializer& s) override
        {
          ModelConfig::serialize(s);
        }

        virtual
        std::unique_ptr<memo::model::Model>
        make(bool,
             boost::filesystem::path const&) override
        {
          return std::make_unique<memo::model::faith::Faith>(
            this->storage->make(), this->version);
        }
      };

      static const elle::serialization::Hierarchy<ModelConfig>::
      Register<FaithModelConfig> _register_FaithModelConfig("faith");
    }
  }
}
