namespace std
{
  inline
  size_t
  hash<boost::asio::ip::udp::endpoint>::operator ()
    (boost::asio::ip::udp::endpoint const& e) const
  {
    // FIXME: why not just hashing address and port?
    return std::hash<std::string>()(e.address().to_string()
                                    + ":" + std::to_string(e.port()));
  }
}
