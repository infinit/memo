#include <infinit/filesystem/AnyBlock.hh>
#include <elle/bench.hh>
#include <cryptography/SecretKey.hh>
#include <infinit/filesystem/umbrella.hh>
#include <infinit/model/doughnut/CHB.hh>
#include <reactor/scheduler.hh>

ELLE_LOG_COMPONENT("infinit.filesystem.AnyBlock");

namespace infinit
{
  namespace filesystem
  {
    AnyBlock::AnyBlock()
    {}
    AnyBlock::AnyBlock(std::unique_ptr<Block> block,
                       std::string const& secret)
    : _backend(std::move(block))
    , _is_mutable(dynamic_cast<MutableBlock*>(_backend.get()))
    {
      _address = _backend->address();
      ELLE_DEBUG("Anyblock mutable=%s, addr = %x", _is_mutable, _backend->address());
      if (!_is_mutable)
      {
        _owner = dynamic_cast<model::doughnut::CHB*>(_backend.get())->owner();
        _buf = _backend->take_data();
        ELLE_DEBUG("Nonmutable, stole %s bytes", _buf.size());
        if (!_buf.empty() && !secret.empty())
        {
          static elle::Bench bench("bench.anyblock.decipher", 10000_sec);
          elle::Bench::BenchScope bs(bench);
          cryptography::SecretKey sk(secret);
          if (_buf.size() < 100000)
          {
            auto res = sk.decipher(_buf);
            _buf = std::move(res);
          }
          else
          {
            reactor::background([&] {
                auto res = sk.decipher(_buf);
                _buf = std::move(res);
            });
          }
        }
      }
    }

    AnyBlock::AnyBlock(AnyBlock && b)
    : _backend(std::move(b._backend))
    , _is_mutable(b._is_mutable)
    , _buf(std::move(b._buf))
    , _address(b._address)
    , _owner(b._owner)
    {

    }
    void AnyBlock::operator = (AnyBlock && b)
    {
      _backend = std::move(b._backend);
     _is_mutable = b._is_mutable;
     _buf = std::move(b._buf);
     _address = b._address;
     _owner = b._owner;
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
        return model.make_block<ImmutableBlock>(std::move(_buf), _owner);
    }

    std::unique_ptr<Block> AnyBlock::make(infinit::model::Model& model,
                                          std::string const& secret)
    {
      cryptography::SecretKey sk(secret);
      if (_buf.size() >= 262144)
        reactor::background([&] {
            _buf = sk.encipher(_buf);
      });
      else
        _buf = sk.encipher(_buf);
      return model.make_block<ImmutableBlock>(_buf, _owner);
    }

    Address AnyBlock::crypt_store(infinit::model::Model& model,
                            infinit::model::StoreMode mode,
                            std::string const& secret)
    {
      ELLE_ASSERT(!_is_mutable);
      {
        static elle::Bench bench("bench.anyblock.encipher", 10000_sec);
        elle::Bench::BenchScope bs(bench);
        cryptography::SecretKey sk(secret);
        if (_buf.size() >= 262144)
          reactor::background([&] {
              _buf = sk.encipher(_buf);
          });
        else
          _buf = sk.encipher(_buf);
      }
      return store(model, mode);
    }

    Address AnyBlock::store(infinit::model::Model& model,
                            infinit::model::StoreMode mode)
    {
      ELLE_DEBUG("Anyblock store: %x", _backend->address());
      if (_is_mutable)
      {
        auto addr = _backend->address();
        umbrella([&] {model.store(std::move(_backend), mode);});
        return addr;
      }
      auto block = model.make_block<ImmutableBlock>(_buf, _owner);
      _address = block->address();
      umbrella([&] { model.store(std::move(block), mode);});
      return _address;
    }
  }
}
