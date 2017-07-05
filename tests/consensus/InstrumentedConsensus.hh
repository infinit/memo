#pragma once

#include <boost/signals2.hpp>

#include <memo/model/MissingBlock.hh>
#include <memo/model/doughnut/Consensus.hh>

class InstrumentedConsensus
  : public memo::model::doughnut::consensus::Consensus
{
public:
  using Address = memo::model::Address;

  InstrumentedConsensus(memo::model::doughnut::Doughnut& dht)
    : memo::model::doughnut::consensus::Consensus(dht)
  {}

public:
  void
  add(memo::model::blocks::Block const& block)
  {
    this->_blocks.emplace(block.address(), block.clone());
  }

  using Blocks = std::unordered_map<
    Address, std::unique_ptr<memo::model::blocks::Block>>;
  ELLE_ATTRIBUTE_R(Blocks, blocks);
  ELLE_ATTRIBUTE_RX(boost::signals2::signal<void(Address const&)>, fetched);

protected:
  void
  _store(std::unique_ptr<memo::model::blocks::Block>,
         memo::model::StoreMode,
         std::unique_ptr<memo::model::ConflictResolver>) override
  {}

  std::unique_ptr<memo::model::blocks::Block>
  _fetch(Address addr, boost::optional<int>) override
  {
    auto it = this->_blocks.find(addr);
    if (it == this->_blocks.end())
      throw memo::model::MissingBlock(addr);
    else
    {
      this->_fetched(addr);
      return it->second->clone();
    }
  }

  void
  _remove(Address addr, memo::model::blocks::RemoveSignature) override
  {
    this->_blocks.erase(addr);
  }
};
