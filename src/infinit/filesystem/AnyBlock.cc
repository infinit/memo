#include <infinit/filesystem/AnyBlock.hh>
#include <infinit/filesystem/umbrella.hh>

ELLE_LOG_COMPONENT("infinit.filesystem.AnyBlock");

namespace infinit
{
  namespace filesystem
  {
    AnyBlock::AnyBlock()
    {}
    AnyBlock::AnyBlock(std::unique_ptr<Block> block)
    : _backend(std::move(block))
    , _is_mutable(dynamic_cast<MutableBlock*>(_backend.get()))
    {
      _address = _backend->address();
      ELLE_DEBUG("Anyblock mutable=%s, addr = %x", _is_mutable, _backend->address());
      if (!_is_mutable)
      {
        _buf = _backend->take_data();
        ELLE_DEBUG("Nonmutable, stole %s bytes", _buf.size());
      }
    }

    AnyBlock::AnyBlock(AnyBlock && b)
    : _backend(std::move(b._backend))
    , _is_mutable(b._is_mutable)
    , _buf(std::move(b._buf))
    , _address(b._address)
    {

    }
    void AnyBlock::operator = (AnyBlock && b)
    {
      _backend = std::move(b._backend);
     _is_mutable = b._is_mutable;
     _buf = std::move(b._buf);
     _address = b._address;
    }
    void AnyBlock::data(std::function<void (elle::Buffer&)> transformation)
    {
      if (_is_mutable)
        dynamic_cast<MutableBlock*>(_backend.get())->data(transformation);
      else
        transformation(_buf);
    }
    elle::Buffer const& AnyBlock::data()
    {
      if (_is_mutable)
        return _backend->data();
      else
        return _buf;
    }
    void AnyBlock::zero(int offset, int count)
    {
      data([&](elle::Buffer& data)
        {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memset(data.mutable_contents() + offset, 0, count);
        });
    }
    void AnyBlock::write(int offset, const void* input, int count)
    {
      data([&](elle::Buffer& data)
         {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memcpy(data.mutable_contents() + offset, input, count);
         });
    }
    void AnyBlock::read(int offset, void* output, int count)
    {
      data([&](elle::Buffer& data)
         {
          if (signed(data.size()) < offset + count)
            data.size(offset + count);
          memcpy(output, data.mutable_contents() + offset, count);
         });
    }
    std::unique_ptr<Block> AnyBlock::take(infinit::model::Model& model)
    {
      if (_is_mutable)
        return std::move(_backend);
      else
        return model.make_block<ImmutableBlock>(std::move(_buf));
    }

    Address AnyBlock::store(infinit::model::Model& model,
                            infinit::model::StoreMode mode)
    {
      ELLE_DEBUG("Anyblock store: %x", _backend->address());
      if (_is_mutable)
      {
        umbrella([&] {model.store(*_backend, mode);});
        return _backend->address();
      }
      auto block = model.make_block<ImmutableBlock>(_buf);
      umbrella([&] { model.store(*block, mode);});
      _address = block->address();
      return _address;
    }
  }
}
