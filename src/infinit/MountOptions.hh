#pragma once

#include <vector>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/optional.hpp>

#include <das/Symbol.hh>
#include <das/model.hh>
#include <das/serializer.hh>

#include <infinit/serialization.hh>

namespace infinit
{
  namespace mount_options
  {
    DAS_SYMBOL(as);
    DAS_SYMBOL(async);
    DAS_SYMBOL(cache);
    DAS_SYMBOL(cache_disk_size);
    DAS_SYMBOL(cache_ram_invalidation);
    DAS_SYMBOL(cache_ram_size);
    DAS_SYMBOL(cache_ram_ttl);
    DAS_SYMBOL(fetch);
    DAS_SYMBOL(fuse_options);
    DAS_SYMBOL(hub_url);
    DAS_SYMBOL(mountpoint);
    DAS_SYMBOL(peers);
    DAS_SYMBOL(poll_beyond);
    DAS_SYMBOL(push);
    DAS_SYMBOL(rdv);
    DAS_SYMBOL(readonly);
  }

  struct MountOptions
  {
    MountOptions();
    void to_commandline(std::vector<std::string>& arguments,
                        std::unordered_map<std::string, std::string>& env) const;
    // void merge(boost::program_options::variables_map const& args);
    void merge(MountOptions const& other);
    boost::optional<std::string> hub_url;
    boost::optional<std::string> rdv;
    boost::optional<std::vector<std::string>> fuse_options;
    boost::optional<std::string> as;
    boost::optional<bool> fetch;
    boost::optional<bool> push;
    boost::optional<bool> cache;
    boost::optional<bool> async;
    boost::optional<bool> readonly;
    boost::optional<int> cache_ram_size;
    boost::optional<int> cache_ram_ttl;
    boost::optional<int> cache_ram_invalidation;
    boost::optional<uint64_t> cache_disk_size;
    boost::optional<std::string> mountpoint;
    boost::optional<std::vector<std::string>> peers;
    boost::optional<int> poll_beyond;
    boost::optional<boost::asio::ip::address> listen_address;
#ifndef INFINIT_WINDOWS
    boost::optional<bool> enable_monitoring;
#endif
    typedef infinit::serialization_tag serialization_tag;
    using Model = das::Model<
      MountOptions,
      decltype(elle::meta::list(
                 mount_options::hub_url,
                 mount_options::rdv,
                 mount_options::fuse_options,
                 mount_options::fetch,
                 mount_options::push,
                 mount_options::cache,
                 mount_options::async,
                 mount_options::readonly,
                 mount_options::cache_ram_size,
                 mount_options::cache_ram_ttl,
                 mount_options::cache_ram_invalidation,
                 mount_options::cache_disk_size,
                 mount_options::mountpoint,
                 mount_options::as,
                 mount_options::peers,
                 mount_options::poll_beyond))>;
  };
}

DAS_SERIALIZE(infinit::MountOptions);
