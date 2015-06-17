#include <arpa/inet.h>

#include <boost/random.hpp>
#include <boost/filesystem/fstream.hpp>

#include <reactor/filesystem.hh>

#include <infinit/model/steg/Steg.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <cryptography/hash.hh>
#include <elle/system/Process.hh>

extern "C" void outguess_extract(
  u_char* key, int key_length, // secret key
  u_char** out, u_int* out_len, // output extracted/decrypted data
  char* filename // input image
  );

extern "C" int outguess_inject(u_char* data, int len, // data to embed
  u_char* key, int key_length,  // secret key
  char* filename // input/output image
  );
ELLE_LOG_COMPONENT("infinit.fs.steg");
namespace infinit
{
  namespace model
  {
    namespace steg
    {

namespace bfs = boost::filesystem;

class InjectionFailure: public reactor::filesystem::Error
{
public:
  InjectionFailure()
  : reactor::filesystem::Error(EIO, "injection failure")
  {}
};


Steg::Steg(boost::filesystem::path const& storage, std::string const& pass)
: _storage_path(storage)
, _passphrase(pass)
{
  _rng.seed(static_cast<unsigned int>(std::time(0)));
  // populate file list
  auto it = bfs::recursive_directory_iterator(_storage_path);
  for (;it != bfs::recursive_directory_iterator(); ++it)
  {
    bfs::path p(*it);
    if (p.extension() == ".jpg" || p.extension() == ".JPG")
    {
      ELLE_DEBUG("caching %s", p);
      _free_blocks.push_back(p);
      auto hash = cryptography::hash::sha256(p.string());
      Address res = Address(hash.contents());
      _cache.insert(std::make_pair(res, p));
    }
  }
}

Address
Steg::_pick() const
{
  if (_free_blocks.empty())
    throw std::bad_alloc();
  boost::random::uniform_int_distribution<> random(0, _free_blocks.size());
  int v = random(_rng);
  boost::filesystem::path p = _free_blocks[v];
  _free_blocks[v] =  _free_blocks[_free_blocks.size() - 1];
  _free_blocks.pop_back();
  auto hash = cryptography::hash::sha256(p.string());
  Address res = Address(hash.contents());
  _cache.insert(std::make_pair(res, p));

  _used_blocks.push_back(res);

  return res;
}

std::unique_ptr<blocks::MutableBlock>
Steg::_make_mutable_block() const
{
  if (!_root)
  {
    _root = _pick();
    _root_data = elle::make_unique<blocks::Block>(*_root);
    Address user_root = _pick();
    _root_data->data().append(user_root.value(), sizeof(Address));
    const_cast<Steg*>(this)->__store(*_root_data);
    return elle::make_unique<blocks::Block>(*_root);
  }
  Address address = _pick();
  auto res = elle::make_unique<blocks::Block>(address);
  return res;
}

void
Steg::_store(blocks::Block& block)
{
  if (block.address() == *_root)
  {
    Address redirect(_root_data->data().contents());
    blocks::Block b(redirect, elle::Buffer(block.data().contents(), block.data().size()));
    __store(b);
  }
  else
    __store(block);
}

void
Steg::__store(blocks::Block& block)
{
  if (block.data().size() == 0)
    return;
  elle::Buffer b;
  b.append("steg", 4);
  int32_t size = block.data().size();
  size = htonl(size);
  b.append(&size, 4);
  b.append(block.data().contents(), block.data().size());
  auto it = _cache.find(block.address());
  if (it == _cache.end())
    throw MissingBlock(block.address());
  int res = outguess_inject(b.mutable_contents(), b.size(),
                  (unsigned char*)_passphrase.c_str(), _passphrase.length(),
                  (char*)it->second.string().c_str());
  if (res < 0)
    throw InjectionFailure();
  ELLE_TRACE("Injected %s bytes from %s", b.size(), it->second);
}

std::unique_ptr<blocks::Block>
Steg::_fetch(Address address) const
{
  if (!_root)
  {
    ELLE_DEBUG("First fetch: %x", address);
    _root = address;
    _root_data = __fetch(address);
    Address redirect(_root_data->data().contents());
    return __fetch(redirect);
  }
  if (_root == address)
  {
    Address redirect(_root_data->data().contents());
    return __fetch(redirect);
  }
  return __fetch(address);
}

std::unique_ptr<blocks::Block>
Steg::__fetch(Address address) const
{

  auto it = _cache.find(address);
  if (it == _cache.end())
    throw MissingBlock(address);
  {
    auto itfree = std::find(_free_blocks.begin(), _free_blocks.end(), it->second);
    // Check if we mistakenly believe the bloc is free
    if (itfree != _free_blocks.end())
    {
      _free_blocks[itfree - _free_blocks.begin()] = _free_blocks[_free_blocks.size() - 1];
      _free_blocks.pop_back();
    }
  }
  unsigned char* d;
  unsigned int dlen;
  outguess_extract((unsigned char*)_passphrase.c_str(), _passphrase.length(),
                   &d, &dlen,
                   (char*)it->second.string().c_str());
  ELLE_TRACE("Extracted %s bytes from %s", dlen, it->second);
  if (memcmp("steg", d, 4))
  {
    ELLE_WARN("Corrupted data");
    free(d);
    return elle::make_unique<blocks::Block>(address);
  }
  int32_t size = *((int32_t*)d + 1);
  size = ntohl(size);
  if (size > dlen - 8)
    throw std::runtime_error("Short read");
  if (size < dlen - 8)
    ELLE_WARN("Too much data: %s > %s", dlen-8, size);
  elle::Buffer data;
  data.append(d+8, size);
  free(d);
  ELLE_DEBUG("fetched %s: %s bytes", it->second, data.size());
  auto res = elle::make_unique<blocks::Block>(address, std::move(data));
  return res;
}

void
Steg::_remove(Address address)
{
  auto it = _cache.find(address);
  if (it == _cache.end())
    throw MissingBlock(address);
  _free_blocks.push_back(it->second);
}

}}}
