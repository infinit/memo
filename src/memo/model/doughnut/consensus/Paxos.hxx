namespace memo
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        template <typename ... Args>
        Paxos::LocalPeer::LocalPeer(
          Paxos& paxos,
          int factor,
          bool rebalance_auto_expand,
          bool rebalance_inspect,
          std::chrono::system_clock::duration node_timeout,
          Doughnut& dht,
          Address id,
          Args&& ... args)
          : doughnut::Peer(dht, id)
          , Paxos::Peer(dht, id)
          , doughnut::Local(dht, id, std::forward<Args>(args) ...)
          , _paxos(paxos)
          , _factor(factor)
          , _rebalance_auto_expand(rebalance_auto_expand)
          , _rebalance_inspect(rebalance_inspect)
          , _node_timeout(node_timeout)
          , _rebalancable()
          , _rebalanced()
          , _rebalance_thread(elle::sprintf("%s: rebalance", this),
                              [this] () { this->_rebalance(); })
        {}

        static constexpr auto default_node_timeout =
          std::chrono::duration_cast<std::chrono::system_clock::duration>(
            10min);

        template <typename ... Args>
        Paxos::Paxos(Args&& ... args)
          : Paxos(
            elle::das::named::prototype(
              consensus::doughnut,
              consensus::replication_factor,
              consensus::lenient_fetch = false,
              consensus::rebalance_auto_expand = true,
              consensus::rebalance_inspect = true,
              consensus::node_timeout = default_node_timeout
              ).call(
                [] (Doughnut& doughnut,
                    int factor,
                    bool lenient_fetch,
                    bool rebalance_auto_expand,
                    bool rebalance_inspect,
                    std::chrono::system_clock::duration node_timeout
                  ) -> Paxos
                {
                  return Paxos(doughnut,
                               factor,
                               lenient_fetch,
                               rebalance_auto_expand,
                               rebalance_inspect,
                               node_timeout
                    );
                }, std::forward<Args>(args)...))
        {}
      }
    }
  }
}
