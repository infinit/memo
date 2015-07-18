#ifndef INFINIT_MODEL_BLOCKS_ACL_BLOCK_HH
# define INFINIT_MODEL_BLOCKS_ACL_BLOCK_HH

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
      protected:
        ACLBlock(Address address);
        ACLBlock(Address address, elle::Buffer data);
        friend class infinit::model::Model;

      /*------------.
      | Permissions |
      `------------*/
      public:
        void
        set_permissions(User const& user,
                        bool read,
                        bool write);
        void
        copy_permissions(ACLBlock& to);
      protected:
        virtual
        void
        _set_permissions(User const& user,
                         bool read,
                         bool write);
        virtual
        void
        _copy_permissions(ACLBlock& to);
      /*--------------.
      | Serialization |
      `--------------*/
      public:
        ACLBlock(elle::serialization::Serializer& input);
        virtual
        void
        serialize(elle::serialization::Serializer& s) override;
      private:
        void
        _serialize(elle::serialization::Serializer& input);
      };
    }
  }
}

#endif
