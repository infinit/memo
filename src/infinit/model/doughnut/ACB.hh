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
        ACLEntry(elle::serialization::SerializerIn& s);
        void serialize(elle::serialization::Serializer& s);
        ACLEntry&
        operator =(ACLEntry&& other) = default;

        bool operator == (ACLEntry const& b) const;

        typedef infinit::serialization_tag serialization_tag;
        static ACLEntry deserialize(elle::serialization::SerializerIn& s);
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
        static_assert(std::is_constructible<ACLEntry, elle::serialization::SerializerIn&>::value, "");

      /*-------------.
      | Construction |
      `-------------*/
      public:
        BaseACB(Doughnut* owner,
            elle::Buffer data = {},
            boost::optional<elle::Buffer> salt = {},
            boost::optional<cryptography::rsa::KeyPair> kp = {});
        BaseACB(Self const& other, bool sealed_copy = true);
        ~BaseACB();
        ELLE_ATTRIBUTE_R(int, editor);
        ELLE_ATTRIBUTE(elle::Buffer, owner_token);
        ELLE_ATTRIBUTE(bool, acl_changed, protected);
        ELLE_ATTRIBUTE_R(std::vector<ACLEntry>, acl_entries);
        ELLE_ATTRIBUTE_R(int, data_version, protected);
        ELLE_ATTRIBUTE(reactor::BackgroundFuture<elle::Buffer>, data_signature);
        ELLE_ATTRIBUTE_R(bool, world_readable);
        ELLE_ATTRIBUTE_R(bool, world_writable);
      protected:
        elle::Buffer const& data_signature() const;

      /*-------.
      | Clone  |
      `-------*/
      public:
        virtual
        std::unique_ptr<blocks::Block>
        clone(bool sealed_copy) const override;

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
        std::pair<int,
                  std::shared_ptr<infinit::cryptography::rsa::KeyPair const>>
        _find_token() const;
        virtual
        bool
        operator ==(blocks::Block const& rhs) const override;

      /*------------.
      | Permissions |
      `------------*/
      public:
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
        _list_permissions(boost::optional<Model const&> model) override;
        virtual
        void
        _set_world_permissions(bool read, bool write) override;
        virtual
        std::pair<bool, bool>
        _get_world_permissions() override;

      /*-----------.
      | Validation |
      `-----------*/
      public:
        using Super::seal;
        // Seal with a specific secret key.
        void
        seal(cryptography::SecretKey const& key);
      protected:
        virtual
        blocks::ValidationResult
        _validate() const override;
        virtual
        void
        _seal() override;
        void
        _seal(boost::optional<cryptography::SecretKey const&> key);
        virtual
        void
        _sign(elle::serialization::SerializerOut& s) const override;
      private:
        elle::Buffer
        _data_sign() const;
      protected:
        virtual
        void
        _data_sign(elle::serialization::SerializerOut& s) const;

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        BaseACB(elle::serialization::SerializerIn& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input);
      };

      typedef BaseACB<blocks::ACLBlock> ACB;
    }
  }
}

#endif
