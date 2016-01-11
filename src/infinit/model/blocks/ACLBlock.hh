#ifndef INFINIT_MODEL_BLOCKS_ACL_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_ACL_BLOCK_HH

# include <cryptography/rsa/KeyPair.hh>

# include <infinit/model/User.hh>
# include <infinit/model/blocks/MutableBlock.hh>

namespace infinit
{
  namespace model
  {
    namespace blocks
    {
      class ACLBlock
        : public MutableBlock
      {
      /*------.
      | Types |
      `------*/
      public:
        typedef ACLBlock Self;
        typedef MutableBlock Super;

      /*-------------.
      | Construction |
      `-------------*/
        ACLBlock(ACLBlock const& other);
      protected:
        ACLBlock(Address address);
        ACLBlock(Address address, elle::Buffer data);
        friend class infinit::model::Model;

      /*-------.
      | Clone  |
      `-------*/
      public:
        virtual
        std::unique_ptr<blocks::Block>
        clone() const override;

      /*------------.
      | Permissions |
      `------------*/
      public:
        void
        set_permissions(User const& user,
                        bool read,
                        bool write
                        );
        void
        set_world_permissions(bool read, bool write);
        std::pair<bool, bool>
        get_world_permissions();
        void
        copy_permissions(ACLBlock& to);

        struct Entry
        {
          Entry() {}
          Entry(std::unique_ptr<User> u, bool r, bool w)
          : user(std::move(u)), read(r), write(w) {}
          Entry(Entry&& b) = default;

          std::unique_ptr<User> user;
          bool read;
          bool write;
        };

        std::vector<Entry>
        list_permissions(boost::optional<Model const&> model);

      protected:
        virtual
        void
        _set_permissions(User const& user,
                         bool read,
                         bool write
                         );
        virtual
        void
        _set_world_permissions(bool read, bool write);
        virtual
        std::pair<bool, bool>
        _get_world_permissions();
        virtual
        void
        _copy_permissions(ACLBlock& to);

        virtual
        std::vector<Entry>
        _list_permissions(boost::optional<Model const&> model);

      /*--------------.
      | Serialization |
      `--------------*/
      public:
        ACLBlock(elle::serialization::Serializer& input,
                 elle::Version const& version);
        virtual
        void
        serialize(elle::serialization::Serializer& s,
                  elle::Version const& version) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input);
      };
    }
  }
}

#endif
