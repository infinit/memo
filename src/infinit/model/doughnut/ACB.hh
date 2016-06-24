#ifndef INFINIT_MODEL_DOUGHNUT_ACB_HH
# define INFINIT_MODEL_DOUGHNUT_ACB_HH

# include <thread>

# include <elle/serialization/fwd.hh>

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/User.hh>
# include <infinit/model/blocks/ACLBlock.hh>
# include <infinit/model/doughnut/OKB.hh>

namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      struct ACBDontWaitForSignature {};

      struct ACLEntry
      {
        infinit::cryptography::rsa::PublicKey key;
        bool read;
        bool write;
        elle::Buffer token;

        ACLEntry(infinit::cryptography::rsa::PublicKey key_,
                 bool read_,
                 bool write_,
                 elle::Buffer token_);
        ACLEntry(ACLEntry const& other);
        ACLEntry(elle::serialization::SerializerIn& s, elle::Version const& v);
        void serialize(elle::serialization::Serializer& s, elle::Version const& v);
        ACLEntry&
        operator =(ACLEntry&& other) = default;

        bool operator == (ACLEntry const& b) const;

        typedef infinit::serialization_tag serialization_tag;
        static ACLEntry deserialize(elle::serialization::SerializerIn& s,
                                    elle::Version const& v);
      };

      template<typename Block>
      class BaseACB
        : public BaseOKB<Block>
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef BaseACB<Block> Self;
        typedef BaseOKB<Block> Super;

        static_assert(!std::is_base_of<boost::optional_detail::optional_tag, ACLEntry>::value, "");
        static_assert(std::is_constructible<ACLEntry, elle::serialization::SerializerIn&, elle::Version const&>::value, "");

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
                cryptography::rsa::KeyPair const& keys);
        BaseACB(Self const& other);
        ~BaseACB();
        ELLE_ATTRIBUTE_R(int, editor);
        ELLE_ATTRIBUTE_R(elle::Buffer, owner_token);
        ELLE_ATTRIBUTE(bool, acl_changed, protected);
        ELLE_ATTRIBUTE_R(std::vector<ACLEntry>, acl_entries);
        ELLE_ATTRIBUTE_R(std::vector<ACLEntry>, acl_group_entries);
        ELLE_ATTRIBUTE_R(std::vector<int>, group_version);
        ELLE_ATTRIBUTE_R(int, data_version, protected);
        ELLE_ATTRIBUTE(std::shared_ptr<reactor::BackgroundFuture<elle::Buffer>>,
                       data_signature);
        ELLE_ATTRIBUTE_R(bool, world_readable);
        ELLE_ATTRIBUTE_R(bool, world_writable);
        ELLE_ATTRIBUTE_R(bool, deleted);
        ELLE_ATTRIBUTE_R(std::shared_ptr<cryptography::rsa::PrivateKey>, sign_key);
      protected:
        elle::Buffer const& data_signature() const;

      /*-------.
      | Clone  |
      `-------*/
      public:
        ELLE_CLONABLE();

      /*--------.
      | Content |
      `--------*/
      protected:
        virtual
        int
        version() const override;
        virtual
        elle::Buffer
        _decrypt_data(elle::Buffer const& data) const override;
        void
        _stored() override;
        virtual
        bool
        operator ==(blocks::Block const& rhs) const override;

      /*------------.
      | Permissions |
      `------------*/
      public:
        virtual
        void
        set_group_permissions(cryptography::rsa::PublicKey const& key,
                        bool read,
                        bool write
                        );
        virtual
        void
        set_permissions(cryptography::rsa::PublicKey const& key,
                        bool read,
                        bool write
                        );
      protected:
        virtual
        void
        _set_permissions(model::User const& key,
                         bool read,
                         bool write
                         ) override;
        virtual
        void
        _copy_permissions(blocks::ACLBlock& to) override;
        virtual
        std::vector<blocks::ACLBlock::Entry>
        _list_permissions(boost::optional<Model const&> model) const override;
        virtual
        void
        _set_world_permissions(bool read, bool write) override;
        virtual
        std::pair<bool, bool>
        _get_world_permissions() override;

      private:
        bool
        _admin_user(cryptography::rsa::PublicKey const& key) const;
        bool
        _admin_group(cryptography::rsa::PublicKey const& key) const;

      /*-----------.
      | Validation |
      `-----------*/
      public:
        using Super::seal;
        // Seal with a specific secret key.
        void
        seal(boost::optional<int> version, cryptography::SecretKey const& key);
      protected:
        virtual
        blocks::ValidationResult
        _validate(Model const& model) const override;
        virtual
        blocks::ValidationResult
        _validate(Model const& model,
                  blocks::Block const& new_block) const override;
        virtual
        void
        _seal(boost::optional<int> version) override;
        void
        _seal(boost::optional<int> version,
              boost::optional<cryptography::SecretKey const&> key);
        class OwnerSignature
          : public Super::OwnerSignature
        {
        public:
          OwnerSignature(BaseACB<Block> const& block);
        protected:
          virtual
          void
          _serialize(elle::serialization::SerializerOut& s,
                     elle::Version const& v);
          ELLE_ATTRIBUTE_R(BaseACB<Block> const&, block);
        };
        virtual
        std::unique_ptr<typename Super::OwnerSignature>
        _sign() const override;
        virtual
        model::blocks::RemoveSignature
        _sign_remove(Model& model) const override;
        virtual
        blocks::ValidationResult
        _validate_remove(Model& model,
                         blocks::RemoveSignature const& rs) const override;
      public:
        blocks::ValidationResult
        validate_admin_keys(Model& model) const;
      protected:
        class DataSignature
        {
        public:
          typedef infinit::serialization_tag serialization_tag;
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
        virtual
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input,
                   elle::Version const& version);
      };

      typedef BaseACB<blocks::ACLBlock> ACB;
    }
  }
}

#endif
