#include <elle/das/bound-method.hh>

#include <memo/model/blocks/ImmutableBlock.hh>
#include <memo/model/blocks/MutableBlock.hh>

#include <memo/utility.hh>

namespace memo
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
    struct Serialize<memo::model::StoreMode>
    {
      using Type = int;
      static inline
      Type
      convert(memo::model::StoreMode mode)
      {
        return mode;
      }

      static inline
      memo::model::StoreMode
      convert(int repr)
      {
        return (memo::model::StoreMode) repr;
      }
    };
  }
}
