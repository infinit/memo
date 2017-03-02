#include <infinit/model/blocks/Block.hh>

namespace infinit
{
  namespace model
  {
    template <typename ... Args>
    Model::Model(Args&& ... args)
      : Model(std::tuple<boost::optional<elle::Version>>(
                elle::das::named::prototype(address = boost::none).map(
                  std::forward<Args>(args)...)))
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
