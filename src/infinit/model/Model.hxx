#include <elle/das/bound-method.hh>

#include <infinit/model/blocks/Block.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/blocks/MutableBlock.hh>
#include <infinit/utility.hh>

namespace infinit
{
  namespace model
  {
    /*-------------.
    | Construction |
    `-------------*/

    template <typename ... Args>
    Model::Model(Args&& ... args)
      : Model(elle::das::named::prototype(model::version = boost::none)
              .template map<boost::optional<elle::Version>>(
                std::forward<Args>(args)...))
    {}

    template <typename Block, typename ... Args>
    std::unique_ptr<Block>
    Model::_construct_block(Args&& ... args)
    {
      return std::unique_ptr<Block>(new Block(std::forward<Args>(args)...));
    }
  }
}

namespace elle
{
  namespace serialization
  {
    template <>
    struct Serialize<infinit::model::StoreMode>
    {
      using Type = int;
      static inline
      Type
      convert(infinit::model::StoreMode mode)
      {
        return mode;
      }

      static inline
      infinit::model::StoreMode
      convert(int repr)
      {
        return (infinit::model::StoreMode) repr;
      }
    };
  }
}
