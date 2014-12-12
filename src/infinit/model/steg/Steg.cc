#include <boost/random.hpp>
#include <boost/filesystem/fstream.hpp>

#include <infinit/model/steg/Steg.hh>
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/blocks/Block.hh>
#include <cryptography/hash.hh>
#include <elle/system/Process.hh>

ELLE_LOG_COMPONENT("infinit.steg");
namespace infinit
{
  namespace model
  {
    namespace steg
    {
      
namespace bfs = boost::filesystem;

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

std::unique_ptr<blocks::Block>
Steg::_make_block() const
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
  auto it = _cache.find(block.address());
  if (it == _cache.end())
    throw MissingBlock(block.address());
  auto tmpData = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  auto tmpImage = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  tmpImage += ".jpg";
  bfs::ofstream ofs(tmpData);
  ofs.write((const char*)block.data().contents(), block.data().size());
  ofs.close();
  // outguess -k secret -d m1K.txt orig.jpg out.jpg
  ELLE_DEBUG("payload: %s, %s bytes", tmpData, block.data().size());
  std::vector<std::string> args = {
    "outguess", "-k", _passphrase,
    "-d", tmpData.string(),
    it->second.string(),
    tmpImage.string()
  };
  elle::system::Process p(args);
  p.wait();
  ELLE_DEBUG("pre: %s", bfs::file_size(tmpImage));
  bfs::rename(tmpImage, it->second);
  ELLE_DEBUG("%s -> %s", tmpImage, it->second);
  ELLE_DEBUG("post: %s", bfs::file_size(it->second));
  bfs::remove(tmpData);
}

std::unique_ptr<blocks::Block>
Steg::_fetch(Address address) const
{
  if (!_root)
  {
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
  auto tmpData = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
  //outguess -k secret -r out.jpg recover.txt
  std::vector<std::string> args = {
    "outguess", "-k", _passphrase,
    "-r", it->second.string(),
    tmpData.string()
  };
  elle::system::Process p(args);
  p.wait();
  elle::Buffer data;
  bfs::ifstream ifs(tmpData);
  char buffer[1024];
  while (true)
  {
    ifs.read(buffer, 1024);
    int cnt = ifs.gcount();
    if (cnt <= 0)
      break;
    data.append(buffer, cnt);
  }
  ELLE_DEBUG("fetched %s: %s bytes", it->second, data.size());
  auto res = elle::make_unique<blocks::Block>(address, std::move(data));
  bfs::remove(tmpData);
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

