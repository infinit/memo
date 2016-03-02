static
std::vector<std::string>
endpoints_from_file(boost::filesystem::path const& path)
{
  try
  {
    boost::filesystem::ifstream f;
    ifnt._open_read(f, path, "", "port file");
    std::vector<std::string> res;
    std::string line;
    while (std::getline(f, line))
    {
      if (line.length())
        res.push_back(line);
    }
    return res;
  }
  catch (elle::Error const& e)
  {
    ELLE_DUMP("unable to read port file: %s", e);
    return {};
  }
}

static
void
port_to_file(uint16_t port, boost::filesystem::path const& path_)
{
  boost::filesystem::ofstream f;
  boost::filesystem::path path(
    path_ == path_.filename() ? boost::filesystem::absolute(path_) : path_);
  ifnt._open_write(f, path, "", "port file", true);
  f << port << std::endl;
}

static
void
endpoints_to_file(std::vector<reactor::network::TCPServer::EndPoint> endpoints,
                  boost::filesystem::path const& path_)
{
  boost::filesystem::ofstream f;
  boost::filesystem::path path(
    path_ == path_.filename() ? boost::filesystem::absolute(path_) : path_);
  ifnt._open_write(f, path, "", "endpoint file", true);
  for (auto const& ep: endpoints)
    f << ep << std::endl;
}
