#include <memo/MountOptions.hh>

#include <elle/printf.hh>

namespace memo
{
  template <typename T>
  void merge(T& a, const T& b)
  {
    if (b)
      a = *b;
  }

  template <typename T>
  void merge(std::vector<T>& a,
             std::vector<T> const& b)
  {
    a.insert(a.end(), b.begin(), b.end());
  }

  void
  MountOptions::merge(MountOptions const& b)
  {
    memo::merge(hub_url, b.hub_url);
    memo::merge(rdv, b.rdv);
    memo::merge(fuse_options, b.fuse_options);
    memo::merge(as, b.as);
    memo::merge(fetch, b.fetch);
    memo::merge(push, b.push);
    memo::merge(cache, b.cache);
    memo::merge(publish, b.publish);
    memo::merge(async, b.async);
    memo::merge(readonly, b.readonly);
    memo::merge(cache_ram_size, b.cache_ram_size);
    memo::merge(cache_ram_ttl, b.cache_ram_ttl);
    memo::merge(cache_ram_invalidation, b.cache_ram_invalidation);
    memo::merge(cache_disk_size, b.cache_disk_size);
    memo::merge(mountpoint, b.mountpoint);
    memo::merge(peers, b.peers);
    memo::merge(poll_beyond, b.poll_beyond);
#ifndef ELLE_WINDOWS
    memo::merge(enable_monitoring, b.enable_monitoring);
#endif
    memo::merge(listen_address, b.listen_address);
  }

  void
  MountOptions::to_commandline(Strings& arguments, Environ& env) const
  {
    if (rdv)
      env.emplace("MEMO_RDV", *rdv);
    if (hub_url)
      env.emplace("MEMO_BEYOND", *hub_url);

    // Append to `arguments`.
    auto args = [&arguments](auto&&... as)
      {
        using swallow = int[];
        (void) swallow{(arguments.emplace_back(as), 0)...};
      };

    if (fuse_options)
      for (auto const& fo: *fuse_options)
        args("--fuse-option", fo);
    if (peers)
      for (auto const& fo: *peers)
        args("--peer", fo);
    if (fetch && *fetch) args("--fetch");
    if (push && *push) args("--push");
    if (publish && *publish) args("--publish");
    if (cache && *cache) args("--cache");
    if (async && *async) args("--async");
    if (readonly && *readonly) args("--readonly");
    if (cache_ram_size) args("--cache-ram-size", std::to_string(*cache_ram_size));
    if (cache_ram_ttl) args("--cache-ram-ttl", std::to_string(*cache_ram_ttl));
    if (cache_ram_invalidation) args("--cache-ram-invalidation", std::to_string(*cache_ram_invalidation));
    if (cache_disk_size) args("--cache-disk-size", std::to_string(*cache_disk_size));
    if (poll_beyond && *poll_beyond >0) args("--poll-hub", std::to_string(*poll_beyond));
#ifndef ELLE_WINDOWS
    if (enable_monitoring && !*enable_monitoring) args("--monitoring=false");
#endif
    if (mountpoint)
      args("--mountpoint", *mountpoint);
    if (as)
      args("--as", *as);
  }

  auto
  MountOptions::to_commandline() const
    -> Strings
  {
    auto res = Strings{};
    auto dummy = Environ{};
    to_commandline(res, dummy);
    return res;
  }

  std::ostream&
  operator<<(std::ostream& os, MountOptions const& mo)
  {
    auto opts = std::vector<std::string>{};
    auto env = std::unordered_map<std::string, std::string>{};
    mo.to_commandline(opts, env);
    return elle::fprintf(os, "MountOptions(%s, %s)", opts, env);
  }
}
