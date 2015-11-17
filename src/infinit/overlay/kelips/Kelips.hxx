#ifndef INFINIT_OVERLAY_KELIPS_HXX
# define INFINIT_OVERLAY_KELIPS_HXX

namespace std
{
  inline
  size_t
  hash<boost::asio::ip::udp::endpoint>::operator ()
    (boost::asio::ip::udp::endpoint const& e) const
  {
    return std::hash<std::string>()(e.address().to_string()
                                    + ":" + std::to_string(e.port()));
  }
}

#endif
