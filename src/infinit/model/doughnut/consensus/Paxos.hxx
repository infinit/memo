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
        Paxos::Paxos(Args&& ... args)
          : Paxos(
            elle::named::prototype(
              consensus::doughnut,
              consensus::replication_factor,
              consensus::lenient_fetch = false).call(
                [] (Doughnut& doughnut,
                    int factor,
                    bool lenient_fetch) -> Paxos
                {
                  return Paxos(doughnut, factor, lenient_fetch);
                }))
        {}
      }
    }
  }
}

#endif
