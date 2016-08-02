#ifndef INFINIT_MODEL_CONSENSUS_PAXOS_HXX
# define INFINIT_MODEL_CONSENSUS_PAXOS_HXX

namespace infinit
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
          , _node_timeout(node_timeout)
          , _rebalancable()
          , _rebalanced()
          , _rebalance_thread(elle::sprintf("%s: rebalance", this),
                              [this] () { this->_rebalance(); })
        {}

        static constexpr
        std::chrono::system_clock::duration default_node_timeout =
          std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::minutes(10));

        template <typename ... Args>
        Paxos::Paxos(Args&& ... args)
          : Paxos(
            elle::named::prototype(
              consensus::doughnut,
              consensus::replication_factor,
              consensus::lenient_fetch = false,
              consensus::rebalance_auto_expand = true,
              consensus::node_timeout = default_node_timeout
              ).call(
                [] (Doughnut& doughnut,
                    int factor,
                    bool lenient_fetch,
                    bool rebalance_auto_expand,
                    std::chrono::system_clock::duration node_timeout
                  ) -> Paxos
                {
                  return Paxos(doughnut,
                               factor,
                               lenient_fetch,
                               rebalance_auto_expand,
                               node_timeout
                    );
                }, std::forward<Args>(args)...))
        {}
      }
    }
  }
}

#endif
