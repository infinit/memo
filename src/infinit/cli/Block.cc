#include <infinit/cli/Block.hh>

#include <infinit/cli/Infinit.hh>
#include <infinit/model/doughnut/consensus/Paxos.hh>


ELLE_LOG_COMPONENT("cli.block");

namespace infinit
{
  namespace cli
  {
    Block::Block(Memo& memo)
      : Object(memo)
      , deserialize(*this,
                    "Deserialized block",
                    cli::output = boost::none,
                    cli::paths = Paths{})
    {}

    /*--------------------.
    | Mode: deserialize.  |
    `--------------------*/

    void
    Block::mode_deserialize(boost::optional<std::string> output_path,
                            Paths const& paths)
    {
      auto output = this->cli().get_output(output_path);
      for (auto const& path: paths)
      {
        boost::filesystem::ifstream f(path);
        if (!f.good())
          elle::err("unable to open for reading: %s", path);
        auto ctx = elle::serialization::Context
          {
            static_cast<infinit::model::doughnut::Doughnut*>(nullptr),
            elle::serialization_tag::version,
          };
        using BlockOrPaxos = infinit::model::doughnut::consensus::BlockOrPaxos;
        auto block
          = elle::serialization::binary::deserialize<BlockOrPaxos>(f, true, ctx);
        elle::serialization::json::serialize(block, *output);
      }
    }
  }
}
