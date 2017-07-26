#include <memo/log.hh>

#include <elle/log/log.hh>

ELLE_LOG_COMPONENT("memo.log");

namespace memo
{
  void make_critical_log()
  {
    auto const log_dir = canonical_folder(xdg_cache_home() / "logs");
    create_directories(log_dir);
    auto const level =
      memo::getenv("LOG_LEVEL",
                   "*athena*:DEBUG,*cli*:DEBUG,*model*:DEBUG"
                   ",*grpc*:DEBUG,*prometheus:LOG"s);
    auto const spec =
      elle::print("file://{file}?"
                  "time,microsec,"
                  "append,size=64MiB,rotate=15,"
                  "{level}",
                  {
                    {"file", (log_dir / "main").string()},
                    {"level", level},
                  });
    ELLE_DUMP("building critical log: {}", spec);
    auto logger = elle::log::make_logger(spec);
    logger->message(elle::log::Logger::Level::log,
                    elle::log::Logger::Type::warning,
                    _trace_component_,
                    std::string(80, '-') + '\n'
                    + std::string(80, '-') + '\n'
                    + std::string(80, '-') + '\n'
                    + "starting memo " + version_describe(),
                    __FILE__, __LINE__, "Memo::Memo");
    elle::log::logger_add(std::move(logger));
  }
}
