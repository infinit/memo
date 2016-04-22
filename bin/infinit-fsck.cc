
#include <infinit/model/MissingBlock.hh>
#include <infinit/model/doughnut/Doughnut.hh>
#include <infinit/filesystem/filesystem.hh>
#include <infinit/filesystem/Directory.hh>
#include <infinit/filesystem/File.hh>
#include <infinit/filesystem/Symlink.hh>
#include <infinit/model/blocks/ImmutableBlock.hh>
#include <infinit/model/doughnut/NB.hh>
#include <infinit/model/doughnut/UB.hh>
#include <infinit/model/doughnut/ACB.hh>
#include <elle/serialization/json.hh>

#include <reactor/scheduler.hh>

ELLE_LOG_COMPONENT("infinit-fsck");

#include <main.hh>

using boost::program_options::variables_map;
using infinit::model::Address;
using infinit::model::MissingBlock;
namespace ifs = infinit::filesystem;

infinit::Infinit ifnt;

struct BlockInfo
{
  BlockInfo()
   : accounted_for(false)
   {}
  bool accounted_for;
};

bool
prompt(std::string const& what)
{
  std::cout << what << std::endl;
  while (true)
  {
    std::string res;
    std::getline(std::cin, res);
    if (res == "y")
      return true;
    else if (res == "n")
      return false;
    else
      std::cout << "Enter y or n" << std::endl;
  }
}

void
account_for(std::unordered_map<Address, BlockInfo>& blocks,
            Address addr, std::string const& info)
{
  auto it = blocks.find(addr);
  if (it == blocks.end())
  {
    ELLE_WARN("Block %x (%s) not in blocklist!", addr, info);
    blocks[addr].accounted_for = true;
  }
  else
    it->second.accounted_for = true;
}

void
fsck(std::unique_ptr<infinit::filesystem::FileSystem>& fs,
     std::unordered_map<Address, BlockInfo>& blocks)
{
  typedef std::shared_ptr<reactor::filesystem::Path> PathPtr;
  // account for root block
  auto dn = std::dynamic_pointer_cast<infinit::model::doughnut::Doughnut>(fs->block_store());
  Address addr = infinit::model::doughnut::NB::address(
    *dn->owner(), fs->volume_name() + ".root", dn->version());
  account_for(blocks, addr, "root block address");
  auto block = fs->block_store()->fetch(addr);
  addr = Address::from_string(block->data().string().substr(2));
  account_for(blocks, addr, "root block");

  std::vector<PathPtr> queue;
  auto root = fs->path("/");
  queue.push_back(root);
  int longest = 0;
  while (!queue.empty())
  {
    auto p = queue.back();
    queue.pop_back();
    if (auto d = std::dynamic_pointer_cast<ifs::Directory>(p))
    {
      std::cerr << '\r' << std::string(longest, ' ');
      std::cerr << '\r' << d->full_path().string();
      longest = std::max(longest, int(d->full_path().string().size()));
      std::vector<std::string> to_remove;
      try
      {
        d->fetch();
      }
      catch (...)
      {
        ELLE_WARN("Unexpected exception fetching %s, skipping: %s",
                    d->full_path(), elle::exception_string());
        continue;
      }
      auto files = d->data()->files();
      for (auto const& e: files)
      {
        try
        {
          auto block = dn->fetch(e.second.second);
        }
        catch (MissingBlock const& mb)
        {
          if (prompt(elle::sprintf("First block missing for %s/%s, delete entry?",
                                   d->full_path().string(), e.first)))
            to_remove.push_back(e.first);
          continue;
        }
        catch (...)
        {
          ELLE_WARN("Unexpected exception fetching %s/%s, skipping: %s",
                    d->full_path().string(), e.first, elle::exception_string());
          continue;
        }
        account_for(blocks, e.second.second, "directory entry");
        try
        {
          auto v = block->validate(*dn);
          if (!v)
          {
            if (prompt(elle::sprintf("Validation failed for %s/%s : %s, delete entry?",
                                   d->full_path(), e.first, v.reason())))
            to_remove.push_back(e.first);
            continue;
          }
          block->data();
        }
        catch (...)
        {
          ELLE_WARN("Unexpected exception reading %s/%s, skipping: %s",
            d->full_path(), e.first, elle::exception_string());
          continue;
        }
        try
        {
          auto c = d->child(e.first);
          queue.push_back(c);
        }
        catch (...)
        {
          ELLE_WARN("Unexpected exception instanciating %s/%s, skipping: %s",
            d->full_path(), e.first, elle::exception_string());
          continue;
        }
      }
      for (auto const& rf : to_remove)
      {
        d->setxattr("user.infinit.fsck.deref", rf, 0);
      }
    }
    else if (auto f = std::dynamic_pointer_cast<ifs::File>(p))
    {
      int idx=-1;
      auto fat = f->filedata()->fat();
      for (auto const& e: fat)
      {
        ++idx;
        try
        {
          auto block = dn->fetch(e.first);
        }
        catch (MissingBlock const& mb)
        {
          if (prompt(elle::sprintf("Fat block missing for %s, nullify?",
                                   f->full_path())))
          {
            f->setxattr("user.infinit.fsck.nullentry",
                        std::to_string(idx), 0);
          }
          continue;
        }
        catch (...)
        {
          ELLE_WARN("Unexpected exception fetching %s, skipping: %s",
                    f->full_path(), elle::exception_string());
          continue;
        }
        account_for(blocks, e.first, "file data block");
      }
    }
    else if (auto s = std::dynamic_pointer_cast<ifs::Symlink>(p))
    {
    }
    else
    {
      ELLE_WARN("Unknown filesystem entry type");
    }
  }

  // process unaccounted for blocks
  try {
    root->child("lost+found")->mkdir(700);
  }
  catch(...) {}
  auto lostfound = root->child("lost+found");
  std::set<Address> chbs;
  std::set<Address> acbs;
  for (auto& b: blocks)
  {
    if (!b.second.accounted_for)
    {
      try
      {
        auto block = dn->fetch(b.first);
        auto val = block->validate(*dn);
        if (!val)
        {
          ELLE_WARN("Validation failed for unaccounted %x: %s",
                    b.first, val.reason());
          continue;
        }
        std::cerr << '\r' << b.first << " " << elle::type_info(*block).name();
        if (dynamic_cast<infinit::model::doughnut::UB*>(block.get())
          || dynamic_cast<infinit::model::doughnut::NB*>(block.get()))
          continue;
        if (dynamic_cast<infinit::model::blocks::ImmutableBlock*>(block.get()))
          chbs.insert(b.first);
        else if (dynamic_cast<infinit::model::doughnut::ACB*>(block.get()))
          acbs.insert(b.first);
        else
        {
          ELLE_WARN("Unknown block type for %x: %s", b.first,
                    elle::type_info(*block).name());
        }
      }
      catch (MissingBlock const& mb)
      {
        ELLE_WARN("Block %x from blocklist missing", b.first);
        continue;
      }
      catch (...)
      {
        ELLE_WARN("Unexpected exception processing unacounted %x: %s",
                  b.first, elle::exception_string());
      }
    }
  }
  if (acbs.empty() && chbs.empty())
    return;
  ELLE_LOG("%s chbs and %s acbs unaccounted for", chbs.size(), acbs.size());
  if (!prompt("Proceed with recovery? This is NOT RECOMENDED if you got any warning above."))
    return;
  // Iterate and remove from acbs entries linked by other acbs
  bool updated = true;
  std::unordered_map<Address, infinit::filesystem::EntryType> types;
  // note: One pass might actually be enough in fact now that I think of it maybe.
  while (updated)
  {
    updated = false;
    auto nacbs = acbs;
    for (auto const& a: nacbs)
    {
      try
      {
        auto block = dn->fetch(a);
        auto buf = block->data();
        elle::IOStream ios(buf.istreambuf());
        elle::serialization::binary::SerializerIn input(ios);
        infinit::filesystem::FileHeader header;
        // Damned, all fs blocks have a header, but they differ in how they
        // serialize it.
        try
        {
          input.serialize_forward(header);
        }
        catch (...)
        {
          auto b = dn->fetch(a);
          ifs::DirectoryData d({}, *b, {true, true});
          header = d.header();
        }
        if (header.mode & S_IFDIR)
        {
          types[a] = infinit::filesystem::EntryType::directory;
          try
          {
            auto b = dn->fetch(a);
            auto dd = std::make_shared<ifs::DirectoryData>(
              boost::filesystem::path(),
              *b,
              std::make_pair(true, true));
            ifs::Directory d(*fs, dd, nullptr, "");
            bool dchange = false;
            d.fetch();
            auto files = d.data()->files();
            for (auto f: files)
            {
              Address fa = f.second.second;
              auto it = blocks.find(fa);
              if (it != blocks.end() && it->second.accounted_for)
              {
                ELLE_LOG("Unlinked directory %x refers linked entry %x as %s, clearing",
                         a, fa, f.first);
                d.setxattr("user.infinit.fsck.deref", f.first, 0);
                dchange = true;
              }
              else if (acbs.find(fa) != acbs.end())
              {
                acbs.erase(fa);
                updated = true;
              }
            }
            if (dchange)
              d.commit();
          }
          catch(...)
          {
            ELLE_WARN("Unexpected error processing directory %x: %s",
                      a, elle::exception_string());
          }
        }
        else if (header.mode & S_IFREG)
        {
          types[a] = infinit::filesystem::EntryType::file;
        }
        else if (header.mode & S_IFLNK)
        {
          types[a] = infinit::filesystem::EntryType::symlink;
        }
        else
        {
          ELLE_WARN("Unknown mode %s in header.", header.mode);
        }
      }
      catch (...)
      {
        ELLE_WARN("Error processing acb %x: %s", a, elle::exception_string());
      }
    }
  }
  // relink to lost+found remaining acbs
  for (auto const& a: acbs)
  {
    if (types.find(a) == types.end())
      continue;
    auto type = types[a];
    std::string stype(type == infinit::filesystem::EntryType::directory ?
      "d" : type == infinit::filesystem::EntryType::file ?
      "f" : "s");
    std::string addrstring = elle::sprintf("%x", a).substr(2);
    lostfound->setxattr("user.infinit.fsck.ref",
                        stype + ":" + addrstring + ":" + addrstring, 0);
  }
  ELLE_LOG("Relinked to lost+found %s block(s).", acbs.size());
}



void
check(variables_map const& args)
{
  auto name = mandatory(args, "name", "network name");
  auto blocklist_file = mandatory(args, "blocklist", "block list");
  std::unordered_map<Address, BlockInfo> blocks;
  std::ifstream ifs(blocklist_file);
  while (ifs.good())
  {
    std::string saddr;
    ifs >> saddr;
    if (saddr.empty())
      continue;
    if (saddr.substr(0,2) == "0x")
      saddr = saddr.substr(2);
    Address addr = Address::from_string(saddr);
    blocks[addr] = BlockInfo();
  }

  auto self = self_user(ifnt, args);
  auto network = ifnt.network_get(name, self);
  std::unordered_map<infinit::model::Address, std::vector<std::string>> hosts;
  bool fetch = aliased_flag(args, {"fetch-endpoints", "fetch"});
  if (fetch)
    beyond_fetch_endpoints(network, hosts);
  bool cache = flag(args, option_cache);
  auto cache_ram_size = optional<int>(args, option_cache_ram_size);
  auto cache_ram_ttl = optional<int>(args, option_cache_ram_ttl);
  auto cache_ram_invalidation =
    optional<int>(args, option_cache_ram_invalidation);
  report_action("running", "network", network.name);
  auto model = network.run(
    hosts, true, cache,
    cache_ram_size, cache_ram_ttl, cache_ram_invalidation, flag(args, "async"));
  auto fs = elle::make_unique<infinit::filesystem::FileSystem>(
    args["volume"].as<std::string>(),
    std::shared_ptr<infinit::model::doughnut::Doughnut>(model.release()));
  fsck(fs, blocks);
}


int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  using boost::program_options::bool_switch;
  Modes modes {
    {
      "check",
      "Check filesystem",
      &check,
      "--name NETWORK",
      {
        { "name,n", value<std::string>(), "network name" },
        { "volume", value<std::string>(), "volume name" },
        { "peer", value<std::vector<std::string>>()->multitoken(),
          "peer to connect to (host:port)" },
        { "async", bool_switch(), "use asynchronous operations" },
        option_cache,
        option_cache_ram_size,
        option_cache_ram_ttl,
        option_cache_ram_invalidation,
        { "fetch-endpoints", bool_switch(),
          elle::sprintf("fetch endpoints from %s", beyond()).c_str() },
        { "fetch,f", bool_switch(), "alias for --fetch-endpoints" },
        { "blocklist,b", value<std::string>(),
          "file containing the list of block addresses" },
      },
    },
  };
  return infinit::main("Infinit filesystem checker", modes, argc, argv);
}
