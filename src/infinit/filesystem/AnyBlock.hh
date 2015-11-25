#ifndef INFINIT_FILESYSTEM_ANYBLOCK_HH
# define INFINIT_FILESYSTEM_ANYBLOCK_HH

#include <infinit/model/Address.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/Model.hh>

#include <elle/log.hh>

namespace infinit
{
  namespace filesystem
  {
    typedef infinit::model::Address Address;
    typedef infinit::model::blocks::Block Block;
    typedef infinit::model::blocks::MutableBlock MutableBlock;
    typedef infinit::model::blocks::ImmutableBlock ImmutableBlock;

    class AnyBlock
    {
      public:
        AnyBlock();
        AnyBlock(std::unique_ptr<Block> block, std::string const& secret = {});
        AnyBlock(AnyBlock && b);

        AnyBlock(AnyBlock const& b) = delete;
        AnyBlock& operator = (const AnyBlock& b) = delete;

        void operator = (AnyBlock && b);

        Address address() {return _address;}
        void data(std::function<void (elle::Buffer&)> transformation);
        const elle::Buffer& data();
        // Output a block of same type, address might change
        std::unique_ptr<Block> take(infinit::model::Model& model);
        Address crypt_store(infinit::model::Model& model,
                            infinit::model::StoreMode mode,
                            std::string const& secret);
        std::unique_ptr<Block> make(infinit::model::Model& model,
                                    std::string const& secret);
        Address store(infinit::model::Model& model, infinit::model::StoreMode mode);

        void zero(int offset, int count);
        void write(int offset, const void* input, int count);
        void read(int offset, void* output, int count);
        std::unique_ptr<Block> _backend;
        bool _is_mutable;
        elle::Buffer _buf;
        Address _address;
    };
  }
}

#endif
