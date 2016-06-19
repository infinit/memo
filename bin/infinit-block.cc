#include <elle/serialization/binary.hh>

ELLE_LOG_COMPONENT("infinit-block");

#include <main.hh>

infinit::Infinit ifnt;

COMMAND(deserialize)
{
  auto paths =
    mandatory<std::vector<std::string>>(args, "path", "path to block");
  auto output = get_output(args);
  for (auto const& path: paths)
  {
    boost::filesystem::ifstream f(path);
    if (!f.good())
    {
      throw elle::Error(
        elle::sprintf("unable to open for reading: %s", path));
    }
    elle::serialization::Context ctx;
    ctx.set<infinit::model::doughnut::Doughnut*>(nullptr);
    ctx.set<elle::Version>(elle::serialization_tag::version);
    auto block = elle::serialization::binary::deserialize<
      infinit::model::doughnut::consensus::BlockOrPaxos>(f, true, ctx);
    elle::serialization::json::serialize(block, *output);
  }
}

int
main(int argc, char** argv)
{
  program = argv[0];
  using boost::program_options::value;
  Modes modes {
    {
      "deserialize",
      "Deserialized block",
      &deserialize,
      "--path PATHS",
      {
        { "path,p", value<std::vector<std::string>>(), "paths to blocks" },
        option_output("block"),
      },
    },
  };
  return infinit::main("Infinit block debug utility", modes, argc, argv,
                       std::string("path"));
}
