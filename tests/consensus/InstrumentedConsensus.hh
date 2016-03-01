#ifndef INSTRUMENTEDCONSENSUS_HH
# define INSTRUMENTEDCONSENSUS_HH

# include <boost/signals2.hpp>

# include <infinit/model/MissingBlock.hh>
# include <infinit/model/doughnut/Consensus.hh>

class InstrumentedConsensus
  : public infinit::model::doughnut::consensus::Consensus
{
public:
  typedef infinit::model::Address Address;

  InstrumentedConsensus(infinit::model::doughnut::Doughnut& dht)
    : infinit::model::doughnut::consensus::Consensus(dht)
  {}

public:
  void
  add(infinit::model::blocks::Block const& block)
  {
    this->_blocks.emplace(block.address(), block.clone());
  }

  typedef std::unordered_map<
    Address, std::unique_ptr<infinit::model::blocks::Block>>
    Blocks;
  ELLE_ATTRIBUTE_R(Blocks, blocks);
  ELLE_ATTRIBUTE_RX(boost::signals2::signal<void(Address const&)>, fetched);

protected:
  virtual
  void
  _store(std::unique_ptr<infinit::model::blocks::Block>,
         infinit::model::StoreMode,
         std::unique_ptr<infinit::model::ConflictResolver>) override
  {}

  virtual
  std::unique_ptr<infinit::model::blocks::Block>
  _fetch(Address addr, boost::optional<int>) override
  {
    auto it = this->_blocks.find(addr);
    if (it == this->_blocks.end())
      throw infinit::model::MissingBlock(addr);
    else
    {
      this->_fetched(addr);
      return it->second->clone();
    }
  }

  virtual
  void
  _remove(Address addr, infinit::model::blocks::RemoveSignature) override
  {
    this->_blocks.erase(addr);
  }
};


#endif
