
namespace infinit
{
  namespace model
  {
    namespace doughnut
    {
      namespace consensus
      {
        template<typename C>
        C*
        StackedConsensus::find(Consensus* top)
        {
          if (auto res = dynamic_cast<C*>(top))
            return res;
          else if (auto res = dynamic_cast<StackedConsensus*>(top))
            return find<C>(res->backend().get());
          else
            return nullptr;
        }
      }
    }
  }
}
