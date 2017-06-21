#pragma once

#include <thread>

#include <elle/serialization/fwd.hh>

#include <elle/cryptography/rsa/KeyPair.hh>

#include <memo/model/User.hh>
#include <memo/model/blocks/ACLBlock.hh>
#include <memo/model/doughnut/OKB.hh>

namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      struct ACBDontWaitForSignature {};

      struct ACLEntry
      {
        elle::cryptography::rsa::PublicKey key;
        bool read;
        bool write;
        elle::Buffer token;

        ACLEntry(elle::cryptography::rsa::PublicKey key_,
                 bool read_,
                 bool write_,
                 elle::Buffer token_);
        ACLEntry(ACLEntry const& other);
        ACLEntry(elle::serialization::SerializerIn& s, elle::Version const& v);
        void serialize(elle::serialization::Serializer& s, elle::Version const& v);
        ACLEntry&
        operator =(ACLEntry&& other) = default;

        bool operator == (ACLEntry const& b) const;

        using serialization_tag = memo::serialization_tag;
        static ACLEntry deserialize(elle::serialization::SerializerIn& s,
                                    elle::Version const& v);
      };

      /// Common implementation for ACB and GB blocks.
      template<typename Block>
      class BaseACB
        : public BaseOKB<Block>
      {
      /*------.
      | Types |
      `------*/
      public:
        using Self = BaseACB<Block>;
        using Super = BaseOKB<Block>;

        static_assert(!std::is_base_of<boost::optional_detail::optional_tag,
                                       ACLEntry>::value, "");
        static_assert(std::is_constructible<ACLEntry,
                                            elle::serialization::SerializerIn&,
                                            elle::Version const&>::value, "");

      /*-------------.
      | Construction |
      `-------------*/
      public:
        BaseACB(Doughnut* owner,
                elle::Buffer data = {},
                boost::optional<elle::Buffer> salt = {});
        BaseACB(Doughnut* owner,
                elle::Buffer data,
                boost::optional<elle::Buffer> salt,
                elle::cryptography::rsa::KeyPair const& keys);
        BaseACB(Self const& other);
        ~BaseACB() override;
        ELLE_ATTRIBUTE_R(int, editor);
        ELLE_ATTRIBUTE_R(elle::Buffer, owner_token);
        ELLE_ATTRIBUTE(bool, acl_changed, protected);
        ELLE_ATTRIBUTE_R(std::vector<ACLEntry>, acl_entries);
        ELLE_ATTRIBUTE_R(std::vector<ACLEntry>, acl_group_entries);
        ELLE_ATTRIBUTE_R(std::vector<int>, group_version);
        ELLE_ATTRIBUTE_R(int, data_version, protected);
        ELLE_ATTRIBUTE(std::shared_ptr<elle::reactor::BackgroundFuture<elle::Buffer>>,
                       data_signature);
        ELLE_ATTRIBUTE_R(bool, world_readable);
        ELLE_ATTRIBUTE_R(bool, world_writable);
        ELLE_ATTRIBUTE_R(bool, deleted);
        ELLE_ATTRIBUTE_R(std::shared_ptr<elle::cryptography::rsa::PrivateKey>, sign_key);
        // Version used for tokens and secrets. Can differ from block version
        ELLE_ATTRIBUTE_R(elle::Version, seal_version);
      protected:
        elle::Buffer const& data_signature() const;

      /*-------.
      | Clone  |
      `-------*/
      public:
        ELLE_CLONABLE();
        int
        version() const override;

      /*--------.
      | Content |
      `--------*/
      protected:
        elle::Buffer
        _decrypt_data(elle::Buffer const& data) const override;
        void
        _stored() override;
        bool
        operator ==(blocks::Block const& rhs) const override;

      /*------------.
      | Permissions |
      `------------*/
      public:
        virtual
        void
        set_group_permissions(elle::cryptography::rsa::PublicKey const& key,
                              bool read,
                              bool write);
        virtual
        void
        set_permissions(elle::cryptography::rsa::PublicKey const& key,
                        bool read,
                        bool write);
      protected:
        void
        _set_permissions(model::User const& key,
                         bool read,
                         bool write) override;
        void
        _copy_permissions(blocks::ACLBlock& to) override;
        std::vector<blocks::ACLBlock::Entry>
        _list_permissions(boost::optional<Model const&> model) const override;
        void
        _set_world_permissions(bool read, bool write) override;
        std::pair<bool, bool>
        _get_world_permissions() const override;

      private:
        bool
        _admin_user(elle::cryptography::rsa::PublicKey const& key) const;
        bool
        _admin_group(elle::cryptography::rsa::PublicKey const& key) const;

      /*-----------.
      | Validation |
      `-----------*/
      public:
        using Super::seal;
        /// Seal with a specific secret key.
        void
        seal(boost::optional<int> version, elle::cryptography::SecretKey const& key);
        /// Seal with a specific version
        void seal(int version);
      protected:
        blocks::ValidationResult
        _validate(Model const& model, bool writing) const override;
        blocks::ValidationResult
        _validate(Model const& model,
                  blocks::Block const& new_block) const override;
        void
        _seal(boost::optional<int> version) override;
        void
        _seal(boost::optional<int> version,
              boost::optional<elle::cryptography::SecretKey const&> key);
        class OwnerSignature
          : public Super::OwnerSignature
        {
        public:
          OwnerSignature(BaseACB<Block> const& block);
        protected:
          void
          _serialize(elle::serialization::SerializerOut& s,
                     elle::Version const& v) override;
          ELLE_ATTRIBUTE_R(BaseACB<Block> const&, block);
        };
        std::unique_ptr<typename Super::OwnerSignature>
        _sign() const override;
        model::blocks::RemoveSignature
        _sign_remove(Model& model) const override;
        blocks::ValidationResult
        _validate_remove(Model& model,
                         blocks::RemoveSignature const& rs) const override;
        blocks::ValidationResult
        _validate_admin_keys(Model const& model) const;
      protected:
        class DataSignature
        {
        public:
          using serialization_tag = memo::serialization_tag;
          DataSignature(BaseACB<Block> const& block);
          virtual
          void
          serialize(elle::serialization::Serializer& s_,
                    elle::Version const& v);
          ELLE_ATTRIBUTE_R(BaseACB<Block> const&, block);
        };
        virtual
        std::unique_ptr<DataSignature>
        _data_sign() const;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        BaseACB(elle::serialization::SerializerIn& input,
                elle::Version const& version);
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input,
                   elle::Version const& version);
      };

      using ACB = BaseACB<blocks::ACLBlock>;
    }
  }
}

