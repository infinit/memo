#pragma once

#include <vector>
#include <unordered_map>

#include <elle/reactor/asio.hh>
#include <boost/optional.hpp>

#include <elle/das/Symbol.hh>
#include <elle/das/model.hh>
#include <elle/das/serializer.hh>

#include <memo/serialization.hh>

namespace memo
{
  namespace mount_options
  {
    ELLE_DAS_SYMBOL(as);
    ELLE_DAS_SYMBOL(async);
    ELLE_DAS_SYMBOL(cache);
    ELLE_DAS_SYMBOL(cache_disk_size);
    ELLE_DAS_SYMBOL(cache_ram_invalidation);
    ELLE_DAS_SYMBOL(cache_ram_size);
    ELLE_DAS_SYMBOL(cache_ram_ttl);
#ifndef MEMO_WINDOWS
    ELLE_DAS_SYMBOL(enable_monitoring); // aka monitoring.
#endif
    ELLE_DAS_SYMBOL(fetch);             // aka fetch_endpoints.
    ELLE_DAS_SYMBOL(fuse_options);      // aka fuse_option.
    ELLE_DAS_SYMBOL(hub_url);
    ELLE_DAS_SYMBOL(listen_address);    // aka listen
    ELLE_DAS_SYMBOL(mountpoint);
    ELLE_DAS_SYMBOL(peers);             // aka peer.
    ELLE_DAS_SYMBOL(poll_beyond);       // aka fetch_endpoints_interval.
    ELLE_DAS_SYMBOL(push);              // aka push_endpoints.
    ELLE_DAS_SYMBOL(publish);           // aka push && fetch.
    ELLE_DAS_SYMBOL(rdv);
    ELLE_DAS_SYMBOL(readonly);
  }

  struct MountOptions
  {
    using Strings = std::vector<std::string>;
    using Environ = std::unordered_map<std::string, std::string>;

    void to_commandline(Strings& arguments, Environ& env) const;
    Strings to_commandline() const;
    void merge(MountOptions const& other);
    boost::optional<std::string> hub_url;
    boost::optional<std::string> rdv;
    boost::optional<Strings> fuse_options;
    boost::optional<std::string> as;
    boost::optional<bool> fetch;
    boost::optional<bool> push;
    boost::optional<bool> publish;
    boost::optional<bool> cache;
    boost::optional<bool> async;
    boost::optional<bool> readonly;
    boost::optional<int> cache_ram_size;
    boost::optional<int> cache_ram_ttl;
    boost::optional<int> cache_ram_invalidation;
    boost::optional<uint64_t> cache_disk_size;
    boost::optional<std::string> mountpoint;
    boost::optional<Strings> peers;
    boost::optional<int> poll_beyond;
    boost::optional<boost::asio::ip::address> listen_address;
#ifndef MEMO_WINDOWS
    boost::optional<bool> enable_monitoring;
#endif
    using serialization_tag = memo::serialization_tag;
    using Model = elle::das::Model<
      MountOptions,
      decltype(elle::meta::list(
                 mount_options::hub_url,
                 mount_options::rdv,
                 mount_options::fuse_options,
                 mount_options::fetch,
                 mount_options::push,
                 mount_options::publish,
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
                 mount_options::poll_beyond
                 // mount_options::listen_address,
#ifndef MEMO_WINDOWS
                 , mount_options::enable_monitoring
#endif
                 ))>;
  };

  /// Print for debugging.
  std::ostream& operator<<(std::ostream& os, MountOptions const& mo);
}

ELLE_DAS_SERIALIZE(memo::MountOptions);
